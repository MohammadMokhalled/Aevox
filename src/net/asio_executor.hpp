#pragma once
// src/net/asio_executor.hpp
//
// INTERNAL — never included by public headers or application code.
//
// Declares AsioExecutor, the Asio-backed implementation of aevox::Executor.
// All Asio types are confined to this file and asio_executor.cpp.
//
// The public boundary is enforced by CMake: asio::asio is linked PRIVATELY to
// aevox_core, so no Asio include paths propagate to consumers.
//
// Design: ADD §3, §4 (AEV-001-arch.md Rev.2)

#include <aevox/executor.hpp> // public interface we implement

// Standalone Asio — ASIO_STANDALONE and ASIO_NO_DEPRECATED defined via CMake.
// These macros are set PRIVATELY on aevox_core, so they never reach consumers.
#include <asio.hpp>

#include <atomic>
#include <cstdint>
#include <future>
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
 */
class AsioExecutor final : public aevox::Executor
{
public:
    /**
     * Constructs the executor with a fully-resolved config.
     *
     * thread_count must be ≥ 1 (the factory resolves hardware_concurrency()
     * before calling this constructor).
     */
    explicit AsioExecutor(aevox::ExecutorConfig config);
    ~AsioExecutor() override;

    [[nodiscard]] std::expected<void, aevox::ExecutorError> listen(
        std::uint16_t                                             port,
        std::move_only_function<aevox::Task<void>(std::uint64_t)> handler) override;

    [[nodiscard]] std::expected<void, aevox::ExecutorError> run() override;

    void stop() noexcept override;

    [[nodiscard]] std::size_t thread_count() const noexcept override;

private:
    // -------------------------------------------------------------------------
    // AcceptLoop — one per listen() call.
    // Owns the acceptor and handler for a single port.
    // -------------------------------------------------------------------------
    struct AcceptLoop
    {
        asio::ip::tcp::acceptor                                   acceptor;
        std::move_only_function<aevox::Task<void>(std::uint64_t)> handler;

        // Non-copyable (implicitly, due to move_only_function).
        // Movable by default. No explicit member declarations to keep this an aggregate.
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
    // Internal coroutine — runs as asio::awaitable<void> on the thread pool.
    // Accepts connections in a loop until the acceptor is closed (stop() called).
    // -------------------------------------------------------------------------
    asio::awaitable<void> run_accept_loop(AcceptLoop& loop);

    // -------------------------------------------------------------------------
    // Data members
    // -------------------------------------------------------------------------
    aevox::ExecutorConfig      config_;
    asio::thread_pool          pool_;         // owns N worker threads
    std::vector<AcceptLoop>    accept_loops_; // one per listen() call
    std::atomic<State>         state_{State::idle};
    std::atomic<std::uint64_t> next_conn_id_{0};

    // Drain timer: promise/future pair used to signal the timer thread.
    // Timer thread calls pool_.stop() after drain_timeout if not cancelled.
    std::promise<void> drain_signal_;
    std::thread        drain_thread_;
};

} // namespace aevox::net
