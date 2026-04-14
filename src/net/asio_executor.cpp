// src/net/asio_executor.cpp
//
// AsioExecutor — Asio-backed implementation of aevox::Executor.
// Acceptor loop, thread pool management, connection dispatch, and drain logic.
//
// Design: ADD §3, §4 (AEV-001-arch.md Rev.2)
// Asio types are PRIVATE to this translation unit and asio_executor.hpp.

#include "asio_executor.hpp"

#include <algorithm> // std::max
#include <format>
#include <stdexcept>
#include <thread>

namespace {

// Fire-and-forget coroutine: starts immediately (suspend_never initial),
// self-destructs on completion (suspend_never final).
// Its promise defines no await_transform, so any awaitable — including
// aevox::Task<void> — can be co_await-ed inside its body.
struct FireAndForget
{
    struct promise_type
    {
        FireAndForget get_return_object() noexcept
        {
            return {};
        }
        std::suspend_never initial_suspend() noexcept
        {
            return {};
        }
        std::suspend_never final_suspend() noexcept
        {
            return {};
        }
        void return_void() noexcept {}
        void unhandled_exception() noexcept
        {
            std::terminate();
        }
    };
};

// Drives a single handler invocation to completion via symmetric transfer.
// Called on a thread-pool thread (posted via asio::post).
FireAndForget dispatch_handler(std::move_only_function<aevox::Task<void>(std::uint64_t)>& handler,
                               std::uint64_t                                              conn_id)
{
    co_await handler(conn_id); // OK: plain coroutine, no await_transform
}

} // anonymous namespace

namespace aevox::net {

// =============================================================================
// Construction / destruction
// =============================================================================

AsioExecutor::AsioExecutor(aevox::ExecutorConfig config)
    : config_{std::move(config)},
      pool_{config_.thread_count} // thread_count already resolved by make_executor()
{
    // Pre-allocate to avoid vector reallocation after run() co_spawns the loops.
    // 8 listen() calls is generous for typical usage.
    accept_loops_.reserve(8);
}

AsioExecutor::~AsioExecutor()
{
    // Unconditionally stop — pool_.stop() is idempotent. Handles the case where
    // the user destroys the executor without completing the stop→run lifecycle.
    pool_.stop();

    // Cancel the drain timer if it is waiting on the promise signal.
    if (drain_thread_.joinable()) {
        // The promise may already be satisfied if run() completed normally.
        // Ignore the exception in that case.
        try {
            drain_signal_.set_value();
        }
        catch (const std::future_error&) {
        }
        drain_thread_.join();
    }

    // CRITICAL (C-1): explicitly join the pool before member destructors run.
    // accept_loops_ (containing the AcceptLoop::handler objects) is declared after
    // pool_ and therefore destroyed before pool_'s destructor calls join(). Pool
    // threads hold raw pointers into accept_loops_ (captured as `handler = &loop.handler`
    // in the co_spawn lambdas). Without this explicit join(), those threads race
    // with accept_loops_'s destructor → use-after-free in the abnormal destruction path.
    pool_.join();

    // Members now destroyed in reverse-declaration order with all pool threads gone.
}

// =============================================================================
// listen() — bind to port and register a connection handler
// =============================================================================

std::expected<void, aevox::ExecutorError> AsioExecutor::listen(
    std::uint16_t port, std::move_only_function<aevox::Task<void>(std::uint64_t)> handler)
{
    // Guard (M-1): listen() is only valid before run() starts. Calling it after run()
    // would (a) potentially invalidate existing AcceptLoop* pointers held by pool threads
    // via vector reallocation, and (b) never co_spawn the new loop (run() only iterates
    // accept_loops_ at startup). Both outcomes are silent data corruption.
    auto s = state_.load(std::memory_order_acquire);
    if (s != State::idle && s != State::configured) {
        return std::unexpected{aevox::ExecutorError::already_running};
    }

    try {
        auto endpoint = asio::ip::tcp::endpoint{asio::ip::tcp::v4(), port};

        asio::ip::tcp::acceptor acceptor{pool_.get_executor()};
        acceptor.open(endpoint.protocol());
        acceptor.set_option(asio::ip::tcp::acceptor::reuse_address{true});
        acceptor.bind(endpoint);
        acceptor.listen(asio::socket_base::max_listen_connections);

        accept_loops_.push_back(AcceptLoop{
            .acceptor = std::move(acceptor),
            .handler  = std::move(handler),
        });

        // Transition from idle → configured on first listen().
        State expected = State::idle;
        state_.compare_exchange_strong(expected, State::configured);

        return {};
    }
    catch (const asio::system_error& e) {
        // Distinguish bind vs. listen failures by checking the error code.
        // asio::error::address_in_use is the canonical bind failure.
        if (e.code() == asio::error::address_in_use || e.code() == asio::error::access_denied) {
            return std::unexpected{aevox::ExecutorError::bind_failed};
        }
        return std::unexpected{aevox::ExecutorError::listen_failed};
    }
}

// =============================================================================
// run_accept_loop() — internal coroutine running on the thread pool
// =============================================================================

asio::awaitable<void> AsioExecutor::run_accept_loop(AcceptLoop& loop)
{
    for (;;) {
        // async_accept with as_tuple gives [error_code, socket] without throwing.
        auto [ec, socket] =
            co_await loop.acceptor.async_accept(asio::as_tuple(asio::use_awaitable));

        if (ec) {
            // operation_aborted = acceptor was closed by stop() — clean exit.
            if (ec == asio::error::operation_aborted) {
                co_return;
            }
            // Any other error: log and continue — one bad accept must not kill
            // the loop (e.g. EMFILE from too many open files is transient).
            // TODO(AEV-004): replace with spdlog when logging is wired.
            continue;
        }

        // TCP_NODELAY: eliminate Nagle's algorithm delay (PRD §3 mandates low latency).
        asio::error_code nodelay_ec;
        socket.set_option(asio::ip::tcp::no_delay{true}, nodelay_ec);
        // Ignore nodelay_ec — if it fails, latency is suboptimal but not fatal.

        // Assign a monotonic connection ID. Relaxed ordering is sufficient:
        // we only need uniqueness, not any happens-before guarantee.
        auto conn_id = next_conn_id_.fetch_add(1, std::memory_order_relaxed);

        // Dispatch the handler to run on the thread pool.
        // We use FireAndForget (defined at file scope) as the dispatch vehicle instead of
        // asio::awaitable<void>, because Asio's awaitable promise restricts co_await to
        // asio::awaitable<T> and async operations. Task<void> is a standard C++20 awaitable
        // but does NOT satisfy Asio's constraints. By using a plain coroutine (FireAndForget)
        // whose promise does NOT define await_transform, we can co_await Task<void> directly.
        asio::post(pool_.get_executor(),
                   [handler = &loop.handler, conn_id] { dispatch_handler(*handler, conn_id); });
    }
}

// =============================================================================
// run() — start the event loop; blocks until stop() + drain complete
// =============================================================================

std::expected<void, aevox::ExecutorError> AsioExecutor::run()
{
    // Guard against double-run.
    State expected = State::configured;
    if (!state_.compare_exchange_strong(expected, State::running)) {
        // Also accept idle → running for executors with no listen() calls (valid edge case).
        State idle = State::idle;
        if (!state_.compare_exchange_strong(idle, State::running)) {
            return std::unexpected{aevox::ExecutorError::already_running};
        }
    }

    // Co_spawn an accept loop coroutine for each registered port.
    // The loops run until their acceptors are closed by stop().
    for (auto& loop : accept_loops_) {
        asio::co_spawn(pool_.get_executor(), run_accept_loop(loop), asio::detached);
    }

    // Block until the pool drains naturally or stop() force-cancels it.
    pool_.join();

    // Signal the drain timer thread to exit (it may have already fired stop()).
    // Setting value on an already-resolved promise is safe — promise is consumed
    // once by the timer thread's future, after which it is not waited on again.
    if (drain_thread_.joinable()) {
        // We may be here because the timer called pool_.stop().
        // Or here because all work finished before the timeout.
        // Either way: signal the timer to stop waiting and join it.
        // The promise may already be satisfied if the drain timer fired pool_.stop()
        // before pool_.join() returned. That's fine — ignore the exception.
        try {
            drain_signal_.set_value();
        }
        catch (const std::future_error&) {
        }
        drain_thread_.join();
    }

    state_.store(State::stopped);
    return {};
}

// =============================================================================
// stop() — signal shutdown; called from any thread
// =============================================================================

void AsioExecutor::stop() noexcept
{
    State expected = State::running;
    if (!state_.compare_exchange_strong(expected, State::draining)) {
        // Not running: either idle, configured, already draining, or stopped.
        // All of these are valid — stop() is idempotent and safe in all states.
        return;
    }

    // 1. Close all acceptors to terminate the accept loops.
    //    asio::ip::tcp::acceptor::close() is thread-safe.
    for (auto& loop : accept_loops_) {
        asio::error_code ec;
        loop.acceptor.close(ec);
        // Ignore ec — loop will see operation_aborted and exit cleanly.
    }

    // 2. Start the drain timer thread.
    //    Creates a new promise each time stop() is called.
    drain_signal_ = std::promise<void>{};
    auto future   = drain_signal_.get_future();
    auto timeout  = config_.drain_timeout;

    drain_thread_ = std::thread([p = std::move(future), timeout, this]() mutable {
        // Wait for run() to signal (natural drain) or for the timeout to expire.
        if (p.wait_for(timeout) == std::future_status::timeout) {
            // Grace period expired — force-stop the thread pool.
            // This cancels pending I/O operations in all in-flight coroutines.
            pool_.stop();
        }
        // If wait_for returned ready: run() already unblocked (natural drain).
        // pool_.join() in run() will return on its own — no pool_.stop() needed.
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
    // noexcept contract: if thread creation fails, std::terminate is correct.
    // An executor that cannot create threads cannot serve requests.
    return std::make_unique<net::AsioExecutor>(std::move(config));
}

// =============================================================================
// to_string() for ExecutorError
// =============================================================================

[[nodiscard]] std::string_view to_string(ExecutorError e) noexcept
{
    switch (e) {
        case ExecutorError::bind_failed:
            return "bind_failed: OS refused to bind to the requested address/port";
        case ExecutorError::listen_failed:
            return "listen_failed: listen() syscall failed";
        case ExecutorError::accept_failed:
            return "accept_failed: accept() call failed";
        case ExecutorError::already_running:
            return "already_running: run() called on an already-running executor";
        case ExecutorError::not_running:
            return "not_running: operation attempted on a stopped executor";
    }
    return "unknown ExecutorError";
}

} // namespace aevox
