#pragma once
// src/net/asio_executor.hpp
//
// INTERNAL — never included by public headers or application code.
//
// Declares AsioExecutor, the Asio-backed implementation of aevox::Executor.
// All Asio types are confined to this file and asio_executor.cpp.
//
// AEV-006 refactor: replaced asio::thread_pool with asio::io_context +
// std::vector<std::jthread> to enable per-thread initialization of the
// thread_local executor bridges required by pool(), sleep(), when_all().
//
// The public boundary is enforced by CMake: asio::asio is linked PRIVATELY to
// aevox_core, so no Asio include paths propagate to consumers.
//
// Design: ADD §3, §4 (AEV-001-arch.md Rev.2, AEV-006-arch.md §4.2)

#include <aevox/executor.hpp> // public interface we implement

#include "asio_tcp_stream.hpp" // AsioTcpStream::make() — used by run_accept_loop()

// Standalone Asio — ASIO_STANDALONE and ASIO_NO_DEPRECATED defined via CMake.
// These macros are set PRIVATELY on aevox_core, so they never reach consumers.
#include <asio.hpp>

#include <atomic>
#include <cstdint>
#include <future>
#include <optional>
#include <thread>
#include <vector>

namespace aevox::net {

/**
 * Asio-backed implementation of aevox::Executor.
 *
 * Internal type — never exposed through any public header.
 * Obtained exclusively through make_executor().
 *
 * Thread-safety:
 *   - listen() and run() must be called from the same thread, in that order.
 *   - stop() is the only method safe to call from another thread while run()
 *     is executing.
 *
 * Lifecycle:
 *   - idle: constructed, no listen() calls yet.
 *   - configured: at least one listen() call completed.
 *   - running: run() is executing; I/O threads are live.
 *   - draining: stop() called; acceptors closed, drain timer running.
 *   - stopped: run() has returned.
 */
class AsioExecutor final : public aevox::Executor
{
public:
    /**
     * Constructs the executor with a fully-resolved config.
     *
     * thread_count must be ≥ 1 (resolved by make_executor()).
     * cpu_pool_threads == 0 means no dedicated CPU pool; pool() posts to the I/O pool.
     */
    explicit AsioExecutor(aevox::ExecutorConfig config);
    ~AsioExecutor() override;

    [[nodiscard]] std::expected<void, aevox::ExecutorError> listen(
        std::uint16_t                                                               port,
        std::move_only_function<aevox::Task<void>(std::uint64_t, aevox::TcpStream)> handler)
        override;

    [[nodiscard]] std::expected<void, aevox::ExecutorError> run() override;

    void stop() noexcept override;

    [[nodiscard]] std::size_t thread_count() const noexcept override;

private:
    // -------------------------------------------------------------------------
    // AcceptLoop — one per listen() call.
    // Owns the acceptor and connection handler for a single port.
    // -------------------------------------------------------------------------
    struct AcceptLoop
    {
        asio::ip::tcp::acceptor                                                     acceptor;
        std::move_only_function<aevox::Task<void>(std::uint64_t, aevox::TcpStream)> handler;
    };

    // -------------------------------------------------------------------------
    // Internal state machine
    // -------------------------------------------------------------------------
    enum class State : int
    {
        idle,
        configured,
        running,
        draining,
        stopped
    };

    // -------------------------------------------------------------------------
    // Internal coroutine — accept loop for a single port.
    // Runs as asio::awaitable<void> on the io_ctx_ thread pool.
    // -------------------------------------------------------------------------
    asio::awaitable<void> run_accept_loop(AcceptLoop& loop);

    // -------------------------------------------------------------------------
    // Data members
    // -------------------------------------------------------------------------

    aevox::ExecutorConfig config_;

    // I/O execution context — replaced asio::thread_pool from AEV-001.
    // io_ctx_.run() is called from each I/O thread in io_threads_.
    asio::io_context io_ctx_;

    // Work guard: keeps io_ctx_ from returning when it runs out of posted work.
    // Reset by stop() to allow natural drain after acceptors are closed.
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_;

    // Dedicated CPU thread pool for aevox::pool() offloads.
    // nullopt when config_.cpu_pool_threads == 0.
    std::optional<asio::thread_pool> cpu_pool_;

    // I/O worker threads — each sets thread-locals then calls io_ctx_.run().
    std::vector<std::jthread> io_threads_;

    std::vector<AcceptLoop>    accept_loops_;
    std::atomic<State>         state_{State::idle};
    std::atomic<std::uint64_t> next_conn_id_{0};

    // Drain timer: promise/future pair signals the timer thread.
    // Timer thread calls io_ctx_.stop() after drain_timeout if not cancelled.
    std::promise<void> drain_signal_;
    std::thread        drain_thread_;
};

} // namespace aevox::net
