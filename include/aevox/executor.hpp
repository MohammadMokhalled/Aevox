#pragma once
// include/aevox/executor.hpp
//
// Public async I/O execution layer for Aevox.
// Defines the ExecutorConfig, ExecutorError enum, Executor abstract interface,
// and the make_executor() factory function.
//
// No Asio types appear in this file. The concrete implementation (AsioExecutor)
// lives entirely in src/net/ and is never visible to application code.
//
// Design: Tasks/architecture/AEV-001-arch.md Rev.2 §3, §7
// PRD §5.5, §5.6 — Executor abstraction, future-proof networking

#include <aevox/config.hpp>
#include <aevox/task.hpp>
#include <aevox/tcp_stream.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string_view>

namespace aevox {

// =============================================================================
// ExecutorConfig
// =============================================================================

/**
 * @brief Configuration for the Asio-backed executor.
 *
 * Pass to `make_executor()`. All fields have sensible defaults for production.
 * Tests should override `thread_count` (to 2) and `drain_timeout` (to 1–2 s)
 * to keep test suites fast.
 *
 * @code
 * // Production
 * auto ex = aevox::make_executor();
 *
 * // Tests
 * auto ex = aevox::make_executor({.thread_count = 2,
 *                                  .drain_timeout = std::chrono::seconds{2}});
 * @endcode
 */
struct ExecutorConfig
{
    /**
     * @brief Number of worker threads in the I/O thread pool.
     *
     * `kDefaultIoThreadCount` (0) resolves to
     * `std::max(1u, std::thread::hardware_concurrency())` at construction time.
     * Set an explicit positive value to pin the thread count.
     *
     * @note Valid config-file range: 0 to 1024.
     */
    std::size_t thread_count{kDefaultIoThreadCount};

    /**
     * @brief Number of threads in the dedicated CPU thread pool.
     *
     * The CPU pool is used exclusively by `aevox::pool(fn)`. I/O threads
     * never run CPU-pool work, and CPU-pool threads never run I/O work.
     *
     * Set to 0 to disable the dedicated CPU pool. When disabled, `aevox::pool(fn)`
     * posts work to the I/O thread pool instead — the callable still does not block
     * the calling coroutine, but it occupies an I/O thread. Useful in tests to
     * avoid spawning extra threads.
     *
     * Default: kDefaultCpuPoolThreads (4). Suits typical workloads such as image
     * resizing, PDF generation, and large JSON serialisation.
     *
     * @note Valid range: 0 to 256.
     */
    std::size_t cpu_pool_threads{kDefaultCpuPoolThreads};

    /**
     * @brief Grace period given to in-flight coroutines after `stop()`.
     *
     * After `stop()` is called the executor stops accepting new connections.
     * In-flight connection coroutines are given this long to complete. If the
     * timeout expires, the I/O context is force-stopped — remaining coroutines
     * have their pending I/O cancelled and frames destroyed.
     *
     * Default: kDefaultDrainTimeout (30 s). Lower for tests.
     *
     * @note Valid range: 1 second to 3600 seconds.
     */
    std::chrono::seconds drain_timeout{kDefaultDrainTimeout};
};

// =============================================================================
// ExecutorError
// =============================================================================

/**
 * @brief Error codes for executor-level failures.
 *
 * Returned via `std::expected<void, ExecutorError>`. Never thrown.
 */
enum class ExecutorError : std::uint8_t
{
    bind_failed,     ///< OS refused to bind to the requested address/port.
    listen_failed,   ///< `listen()` syscall failed after successful bind.
    accept_failed,   ///< An individual `accept()` call failed (non-fatal, logged).
    already_running, ///< `run()` was called on an already-running executor.
    not_running,     ///< An operation was attempted on a stopped executor.
};

/**
 * @brief Returns a human-readable description of an ExecutorError value.
 *
 * @param e  The error to describe.
 * @return   A null-terminated string literal describing the error.
 *           The lifetime of the returned view is static.
 */
[[nodiscard]] std::string_view to_string(ExecutorError e) noexcept;

// =============================================================================
// ConnectionHandler concept
// =============================================================================

/**
 * @brief Concept satisfied by any callable that handles a new TCP connection.
 *
 * The executor invokes the handler exactly once per accepted connection, passing
 * a monotonically increasing 64-bit connection ID and an owned `TcpStream` for
 * the accepted socket. The handler must return an `aevox::Task<void>` coroutine.
 * Callbacks are not permitted (PRD §6.5).
 *
 * The `TcpStream` is passed by value — the handler takes ownership. The executor
 * does not use the socket after the handler is invoked.
 *
 * @code
 * auto handler = [](std::uint64_t id, aevox::TcpStream stream) -> aevox::Task<void> {
 *     auto result = co_await stream.read();
 *     // ... parse and respond ...
 *     co_return;
 * };
 * static_assert(aevox::ConnectionHandler<decltype(handler)>);
 * @endcode
 */
template <typename F>
concept ConnectionHandler = requires(F f, std::uint64_t conn_id, aevox::TcpStream stream) {
    {
        f(conn_id, std::move(stream))
    } -> std::same_as<Task<void>>;
};

// =============================================================================
// Executor — abstract interface
// =============================================================================

/**
 * @brief Abstract interface for the Aevox I/O execution layer.
 *
 * `Executor` decouples all higher-level Aevox code from the underlying async
 * I/O library (currently Asio; future: `std::net` in C++29 per ADR-1). No code
 * above this interface knows about `asio::io_context` or any OS-specific
 * primitive. The concrete implementation (`AsioExecutor`) lives in `src/net/`
 * and is never visible to callers.
 *
 * **Typical usage:**
 * @code
 * auto ex = aevox::make_executor();
 * ex->listen(8080, [](std::uint64_t id, aevox::TcpStream stream) -> aevox::Task<void> {
 *     auto result = co_await stream.read();
 *     // ... parse and respond ...
 *     co_return;
 * });
 * ex->run();  // blocks until stop() is called
 * @endcode
 *
 * @note Thread-safety: `listen()` and `run()` must be called from the same
 *       thread in that order. After `run()` starts, only `stop()` may be called
 *       from other threads. All other methods are single-threaded.
 * @note Ownership: not copyable or movable. Manage via `std::unique_ptr<Executor>`
 *       returned by `make_executor()`.
 */
class Executor
{
public:
    virtual ~Executor() = default;

    Executor(const Executor&)            = delete;
    Executor& operator=(const Executor&) = delete;
    Executor(Executor&&)                 = delete;
    Executor& operator=(Executor&&)      = delete;

    /**
     * @brief Binds to a TCP port and registers a connection handler.
     *
     * Must be called before `run()`. May be called multiple times to listen on
     * multiple ports — each call registers its own accept loop. The handler is
     * moved into the executor; the caller must not use it after this call.
     *
     * @param port     TCP port number (1–65535). Pass 0 to let the OS assign an
     *                 ephemeral port (useful in tests — query the actual port via
     *                 the OS after the call succeeds).
     * @param handler  Invoked once per accepted connection with a monotonically
     *                 increasing connection ID and an owned `TcpStream`. The executor
     *                 transfers socket ownership to the handler. The caller does not
     *                 need to keep the handler alive after this call.
     * @return         `std::expected<void, ExecutorError>`:
     *                 - `ExecutorError::bind_failed` if the address is unavailable.
     *                 - `ExecutorError::listen_failed` on syscall failure.
     *                 - Empty (success) otherwise.
     */
    [[nodiscard]] virtual std::expected<void, ExecutorError> listen(
        std::uint16_t                                                 port,
        std::move_only_function<Task<void>(std::uint64_t, TcpStream)> handler) = 0;

    /**
     * @brief Runs the event loop, blocking until `stop()` is called.
     *
     * Starts all thread pool workers and all registered accept loops. Returns
     * only after `stop()` is called and either:
     * - All in-flight coroutines complete within `ExecutorConfig::drain_timeout`, OR
     * - The drain timeout expires and `pool_.stop()` force-cancels remaining work.
     *
     * @return `ExecutorError::already_running` if called while already running.
     *         Empty (success) once the executor has fully stopped.
     */
    [[nodiscard]] virtual std::expected<void, ExecutorError> run() = 0;

    /**
     * @brief Signals the executor to stop accepting connections and begin draining.
     *
     * Thread-safe. Safe to call from a signal handler or any thread while `run()`
     * is executing.
     *
     * Sequence:
     * 1. All accept loops close immediately (no new connections accepted).
     * 2. In-flight coroutines are given `ExecutorConfig::drain_timeout` to finish.
     * 3. If the timeout expires, the thread pool is force-stopped: remaining
     *    coroutines have pending I/O cancelled and frames destroyed.
     * 4. `run()` returns.
     *
     * Calling `stop()` before `run()` is a no-op. Calling it multiple times is safe.
     */
    virtual void stop() noexcept = 0;

    /**
     * @brief Returns the number of worker threads in the pool.
     *
     * Equal to `ExecutorConfig::thread_count` as resolved (after
     * `hardware_concurrency()` substitution for the default value 0).
     *
     * @return Worker thread count. Always ≥ 1.
     */
    [[nodiscard]] virtual std::size_t thread_count() const noexcept = 0;

protected:
    Executor() = default; ///< Protected — only concrete subclasses may construct.
};

// =============================================================================
// Factory
// =============================================================================

/**
 * @brief Creates the default Asio-backed Executor.
 *
 * The concrete type (`AsioExecutor`) is hidden in `src/net/` and never named
 * by callers. Passing `ExecutorConfig{}` gives production defaults:
 * - `thread_count = hardware_concurrency()`
 * - `drain_timeout = 30s`
 *
 * @param config  Thread count and drain timeout. All fields have defaults.
 * @return        Owning `std::unique_ptr<Executor>`. Never null — failure to
 *                create OS threads is unrecoverable and calls `std::terminate`.
 *
 * @note `noexcept` because `std::terminate` is the correct response to OS thread
 *       exhaustion at startup. Do not catch or handle thread creation failures.
 */
[[nodiscard]] std::unique_ptr<Executor> make_executor(ExecutorConfig config = {}) noexcept;

} // namespace aevox
