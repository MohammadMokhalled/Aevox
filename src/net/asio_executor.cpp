// src/net/asio_executor.cpp
//
// AsioExecutor — Asio-backed implementation of aevox::Executor.
// Acceptor loop, thread pool management, connection dispatch, and drain logic.
//
// Refactor: replaced asio::thread_pool with asio::io_context + strand dispatch.
//   - Replaced asio::thread_pool with asio::io_context + std::jthread vector.
//     Reason: asio::thread_pool provides no thread-start hook. Manual jthread
//     management allows each I/O thread to set the thread_local executor bridges
//     (tl_post_to_cpu, tl_post_to_io, tl_schedule_after) before processing work.
//   - Added optional<asio::thread_pool> cpu_pool_ for aevox::pool() offloads.
//   - Added optional<work_guard> to control io_ctx_ lifetime.
//
// Design: Tasks/architecture/AEV-006-arch.md §4.2
// Asio types are PRIVATE to this translation unit and asio_executor.hpp.

#include "asio_executor.hpp"

#include <aevox/async.hpp> // for aevox::detail::tl_post_to_* thread-locals
#include <aevox/error.hpp>
#include <aevox/executor.hpp>
#include <aevox/task.hpp>
#include <aevox/tcp_stream.hpp>

#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/impl/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
#include <asio/socket_base.hpp>
#include <asio/steady_timer.hpp>
#include <asio/system_error.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>

namespace {

// Reserve capacity for the accept-loops vector; covers typical multi-port scenarios.
constexpr std::size_t kAcceptLoopsReserveSize{8};

// Drives a single handler invocation to completion via symmetric transfer.
// Uses aevox::detail::FireAndForget from <aevox/async.hpp> (included above).
// Takes handler by reference — the AcceptLoop owns it and outlives the call.
// TcpStream is passed by value (move) — handler takes ownership of the socket.
aevox::detail::FireAndForget dispatch_handler(
    std::move_only_function<aevox::Task<void>(std::uint64_t, aevox::TcpStream)>& handler,
    std::uint64_t conn_id, aevox::TcpStream stream)
{
    co_await handler(conn_id, std::move(stream));
}

} // anonymous namespace

namespace aevox::net {

// =============================================================================
// Construction / destruction
// =============================================================================

AsioExecutor::AsioExecutor(aevox::ExecutorConfig config) : config_{config}
{
    // Pre-allocate to avoid vector reallocation after run() co_spawns the loops.
    accept_loops_.reserve(kAcceptLoopsReserveSize);
    io_threads_.reserve(config_.thread_count);

    // cpu_pool_ is created in run() (not here) to match the thread startup
    // sequencing — both pools must be constructed before any thread touches them.
}

AsioExecutor::~AsioExecutor()
{
    // Unconditionally stop the I/O context — idempotent.
    // Handles the case where the user destroys the executor without calling stop()+run().
    io_ctx_.stop();

    // Join all I/O threads before member destruction.
    // io_threads_ elements (std::jthread) call join in their destructors,
    // but explicit reset ensures ordering with the io_ctx_ and cpu_pool_ members.
    for (auto& t : io_threads_) {
        if (t.joinable())
            t.join();
    }

    // Join cpu_pool_ (if present) — waits for all CPU pool threads.
    if (cpu_pool_)
        cpu_pool_->join();

    // Cancel the drain timer thread if still running.
    if (drain_thread_.joinable()) {
        if (!drain_signaled_.exchange(true))
            drain_signal_.set_value();
        drain_thread_.join();
    }

    // work_guard_ destroyed here (via optional dtor) — no explicit reset needed.
    // accept_loops_ destroyed here — all I/O threads are already joined, so no
    // thread holds a pointer into accept_loops_ at this point.
}

// =============================================================================
// listen() — bind to port and register a connection handler
// =============================================================================

std::expected<void, aevox::ExecutorError> AsioExecutor::listen(
    std::uint16_t                                                               port,
    std::move_only_function<aevox::Task<void>(std::uint64_t, aevox::TcpStream)> handler)
{
    // Guard: listen() is only valid before run() starts.
    auto s = state_.load(std::memory_order_acquire);
    if (s != State::Idle && s != State::Configured) {
        return std::unexpected{aevox::ExecutorError::AlreadyRunning};
    }

    try {
        auto endpoint = asio::ip::tcp::endpoint{asio::ip::tcp::v4(), port};

        asio::ip::tcp::acceptor acceptor{io_ctx_};
        acceptor.open(endpoint.protocol());
        acceptor.set_option(asio::ip::tcp::acceptor::reuse_address{true});
        acceptor.bind(endpoint);
        acceptor.listen(asio::socket_base::max_listen_connections);

        accept_loops_.push_back(AcceptLoop{
            .acceptor = std::move(acceptor),
            .handler  = std::move(handler),
        });

        // Transition from idle → configured on first listen().
        State expected = State::Idle;
        state_.compare_exchange_strong(expected, State::Configured, std::memory_order_release,
                                       std::memory_order_relaxed);
        return {};
    }
    catch (const asio::system_error& e) {
        if (e.code() == asio::error::address_in_use || e.code() == asio::error::access_denied) {
            return std::unexpected{aevox::ExecutorError::BindFailed};
        }
        return std::unexpected{aevox::ExecutorError::ListenFailed};
    }
}

// =============================================================================
// run_accept_loop() — internal coroutine running on the I/O context
// =============================================================================

asio::awaitable<void> AsioExecutor::run_accept_loop(AcceptLoop& loop)
{
    for (;;) {
        auto [ec, socket] =
            co_await loop.acceptor.async_accept(asio::as_tuple(asio::use_awaitable));

        if (ec) {
            if (ec == asio::error::operation_aborted || ec == asio::error::bad_descriptor)
                co_return; // stop() closed the acceptor — clean exit

            // Transient error (e.g. EMFILE) — log and continue.
            // TODO: replace with spdlog when logging is wired.
            continue;
        }

        // TCP_NODELAY: eliminate Nagle's algorithm delay (PRD §3 mandates low latency).
        asio::error_code nodelay_ec;
        socket.set_option(asio::ip::tcp::no_delay{true}, nodelay_ec);

        auto conn_id = next_conn_id_.fetch_add(1, std::memory_order_relaxed);

        // Construct TcpStream from the accepted socket.
        // TcpStreamFactory::make() calls TcpStream's private constructor via the friend
        // declaration.
        auto stream = aevox::net::TcpStreamFactory::make(std::move(socket), io_ctx_);

        // Dispatch the handler to run on the I/O pool.
        // dispatch_handler uses FireAndForget (a plain coroutine without
        // await_transform) so it can co_await Task<void> directly.
        // TcpStream is captured by move — the lambda takes ownership of the socket.
        asio::post(io_ctx_, [&loop, conn_id, stream = std::move(stream)]() mutable {
            dispatch_handler(loop.handler, conn_id, std::move(stream));
        });
    }
}

// =============================================================================
// run() — start the event loop; blocks until stop() + drain complete
// =============================================================================

std::expected<void, aevox::ExecutorError> AsioExecutor::run()
{
    auto current = state_.load(std::memory_order_acquire);
    if (current == State::Draining || current == State::Stopped) {
        state_.store(State::Stopped, std::memory_order_release);
        return {};
    }

    if (current != State::Configured && current != State::Idle) {
        return std::unexpected{aevox::ExecutorError::AlreadyRunning};
    }

    work_guard_.emplace(asio::make_work_guard(io_ctx_));

    // Guard against double-run.
    if (!state_.compare_exchange_strong(current, State::Running, std::memory_order_acq_rel,
                                        std::memory_order_relaxed))
    {
        work_guard_.reset();
        if (current == State::Draining || current == State::Stopped) {
            state_.store(State::Stopped, std::memory_order_release);
            return {};
        }
        return std::unexpected{aevox::ExecutorError::AlreadyRunning};
    }

    // Create the CPU pool (if requested). Done here (not in constructor) so
    // both io_ctx_ and cpu_pool_ are fully constructed before any thread runs.
    if (config_.cpu_pool_threads > 0) {
        cpu_pool_.emplace(config_.cpu_pool_threads);
    }

    // -------------------------------------------------------------------------
    // Start I/O worker threads.
    //
    // Each thread:
    //   1. Sets the three thread_local executor bridge functions (tl_post_to_*)
    //      so that aevox::pool(), sleep(), when_all() work correctly.
    //   2. Calls io_ctx_.run() — blocks until the io_ctx_ is stopped or
    //      exhausted.
    //
    // Why lambda capture instead of thread-local Asio handles:
    //   The asio executor handles (asio::io_context::executor_type,
    //   asio::thread_pool::executor_type) are captured by value and bound
    //   into the std::function closures. The closures are valid for the
    //   thread's lifetime — the io_ctx_ and cpu_pool_ outlive all threads.
    // -------------------------------------------------------------------------

    auto io_exec = io_ctx_.get_executor();

    std::optional<std::reference_wrapper<asio::thread_pool>> cpu_pool_ref;
    if (cpu_pool_) {
        cpu_pool_ref = std::ref(*cpu_pool_);
    }

    for (std::size_t i = 0; i < config_.thread_count; ++i) {
        io_threads_.emplace_back([this, io_exec, cpu_pool_ref]() {
            // Bind tl_post_to_io — posts any callable to the I/O pool.
            // Takes std::move_only_function<void()> so callers can pass
            // move-only lambdas (e.g. those capturing aevox::Task<T>).
            detail::tl_post_to_io() = [io_exec](std::move_only_function<void()> fn) mutable {
                asio::post(io_exec, std::move(fn));
            };

            // Bind tl_post_to_cpu — posts to CPU pool (or I/O pool if disabled).
            if (cpu_pool_ref) {
                auto cpu_exec            = cpu_pool_ref->get().get_executor();
                detail::tl_post_to_cpu() = [cpu_exec](std::move_only_function<void()> fn) mutable {
                    asio::post(cpu_exec, std::move(fn));
                };
            }
            else {
                // No dedicated CPU pool — reuse I/O pool binding.
                detail::tl_post_to_cpu() = [io_exec](std::move_only_function<void()> fn) mutable {
                    asio::post(io_exec, std::move(fn));
                };
            }

            // Bind tl_schedule_after — creates a steady_timer and posts
            // the callable on expiry. The shared_ptr keeps the timer alive
            // until it fires, even if the awaitable is destroyed.
            detail::tl_schedule_after() = [io_exec](std::chrono::steady_clock::duration dur,
                                                    std::move_only_function<void()> fn) mutable {
                auto timer = std::make_shared<asio::steady_timer>(io_exec, dur);
                timer->async_wait(
                    [timer, fn = std::move(fn)](const asio::error_code&) mutable { fn(); });
            };

            // Enter the I/O event loop. Blocks until io_ctx_ is stopped.
            io_ctx_.run();
        });
    }

    // Spawn the accept loop coroutines on the I/O context.
    for (auto& loop : accept_loops_) {
        asio::co_spawn(io_ctx_, run_accept_loop(loop), asio::detached);
    }

    // Block until all I/O threads exit (io_ctx_.run() returns in each).
    for (auto& t : io_threads_) {
        if (t.joinable())
            t.join();
    }

    // Signal the drain timer thread to exit if it hasn't already fired.
    if (drain_thread_.joinable()) {
        if (!drain_signaled_.exchange(true))
            drain_signal_.set_value();
        drain_thread_.join();
    }

    state_.store(State::Stopped, std::memory_order_release);
    return {};
}

// =============================================================================
// stop() — signal shutdown; callable from any thread
// =============================================================================

void AsioExecutor::stop() noexcept
{
    State expected = State::Running;
    if (!state_.compare_exchange_strong(expected, State::Draining, std::memory_order_acq_rel,
                                        std::memory_order_relaxed))
    {
        expected = State::Configured;
        if (!state_.compare_exchange_strong(expected, State::Stopped, std::memory_order_acq_rel,
                                            std::memory_order_relaxed))
        {
            // Not running: either idle, already draining, or stopped.
            // stop() is idempotent in all of these states.
            return;
        }

        for (auto& loop : accept_loops_) {
            asio::error_code ec;
            loop.acceptor.close(ec);
        }
        io_ctx_.stop();
        return;
    }

    // 1. Close all acceptors — terminates the accept loop coroutines.
    //    Post the close to io_ctx_ so it runs serialized with async_accept
    //    initiations on an I/O thread. Calling acceptor.close() from another
    //    thread races with the accept loop's start_op path: Asio's
    //    epoll_reactor::close frees the descriptor_state synchronously, and an
    //    I/O thread locking descriptor_state->mutex_ in start_op would then
    //    dereference a freed pointer (SEGV observed under ASan/UBSan).
    asio::post(io_ctx_, [this] {
        for (auto& loop : accept_loops_) {
            asio::error_code ec;
            loop.acceptor.close(ec);
            // Ignore ec — loop sees operation_aborted and exits cleanly.
        }
    });

    // 2. Release the work guard — io_ctx_ can now drain naturally.
    //    In-flight coroutines continue to run; io_ctx_.run() returns
    //    once all pending handlers complete (or drain_timeout expires).
    work_guard_.reset();

    // 3. Start the drain timer thread.
    //    If in-flight coroutines don't finish within drain_timeout, force-stop.
    drain_signaled_.store(false);
    drain_signal_ = std::promise<void>{};
    auto future   = drain_signal_.get_future();
    auto timeout  = config_.drain_timeout;

    drain_thread_ = std::thread([f = std::move(future), timeout, this]() mutable {
        if (f.wait_for(timeout) == std::future_status::timeout) {
            // Grace period expired — force-cancel remaining I/O operations.
            io_ctx_.stop();
        }
        // If wait_for returned ready: run() signalled completion.
        // io_ctx_.run() will return naturally — no force-stop needed.
    });
}

// =============================================================================
// thread_count()
// =============================================================================

std::size_t AsioExecutor::thread_count() const noexcept
{
    return config_.thread_count;
}

} // namespace aevox::net

// =============================================================================
// Public factory — defined at namespace aevox level (not aevox::net)
// =============================================================================

namespace aevox {

[[nodiscard]] std::unique_ptr<Executor> make_executor(ExecutorConfig config) noexcept
{
    // Resolve thread_count: 0 means hardware_concurrency(), clamped to ≥ 1.
    if (config.thread_count == 0) {
        config.thread_count = std::max(1u, std::thread::hardware_concurrency());
    }
    // noexcept contract: thread creation failure is unrecoverable → std::terminate.
    return std::make_unique<net::AsioExecutor>(config);
}

// =============================================================================
// to_string() for ExecutorError
// =============================================================================

[[nodiscard]] std::string_view to_string(ExecutorError e) noexcept
{
    switch (e) {
        case ExecutorError::BindFailed:
            return "bind_failed: OS refused to bind to the requested address/port";
        case ExecutorError::ListenFailed:
            return "listen_failed: listen() syscall failed";
        case ExecutorError::AcceptFailed:
            return "accept_failed: accept() call failed";
        case ExecutorError::AlreadyRunning:
            return "already_running: run() called on an already-running executor";
        case ExecutorError::NotRunning:
            return "not_running: operation attempted on a stopped executor";
    }
    return "unknown ExecutorError";
}

ErrorCategory category(ExecutorError e) noexcept
{
    switch (e) {
        case ExecutorError::BindFailed:
        case ExecutorError::ListenFailed:
        case ExecutorError::AcceptFailed:
            return ErrorCategory::Io;
        case ExecutorError::AlreadyRunning:
        case ExecutorError::NotRunning:
            return ErrorCategory::State;
    }
    return ErrorCategory::Unknown;
}

} // namespace aevox
