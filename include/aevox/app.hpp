#pragma once
// include/aevox/app.hpp
//
// Public aevox::App class — top-level framework entry point.
//
// App owns the Router and Executor. Route registration methods
// (get/post/…) forward to the internal Router. listen() binds the
// Executor to the configured port and blocks until SIGINT/SIGTERM
// or an explicit stop() call.
//
// Thread-safety:
//   Not thread-safe. All route registration and listen() calls must
//   originate from the main thread. After listen() starts accepting
//   connections, Router::dispatch() is called concurrently from worker
//   threads — this is safe per the Router contract.
//
// Move semantics:
//   Move-only. A moved-from App must not be used.
//
// Design: AEV-004-arch.md §3.2

#include <aevox/executor.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/router.hpp>
#include <aevox/task.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace aevox {

// =============================================================================
// AppConfig
// =============================================================================

/**
 * @brief Configuration for `aevox::App`.
 *
 * All fields have sensible production defaults. Override with designated
 * initialisers:
 * @code
 * aevox::App app({ .port = 9090, .executor = { .thread_count = 8 } });
 * @endcode
 */
struct AppConfig
{
    /** @brief TCP port to bind. Default: 8080. */
    std::uint16_t port{8080};

    /**
     * @brief Bind address. Default: `"0.0.0.0"` (all interfaces).
     *
     * Pass `"127.0.0.1"` for loopback-only. IPv6 is not supported in v0.1.
     */
    std::string host{"0.0.0.0"};

    /** @brief TCP listen backlog depth. Default: 1024. */
    int backlog{1024};

    /**
     * @brief Enable `SO_REUSEPORT` for multi-process deployment. Default: `true`.
     *
     * When `true`, multiple Aevox processes on the same port are load-balanced
     * by the OS kernel. Requires Linux kernel ≥ 3.9.
     */
    bool reuse_port{true};

    /** @brief Executor (thread pool) configuration. */
    ExecutorConfig executor{};

    /**
     * @brief Maximum allowed request body size in bytes. Default: 10 MiB.
     *
     * Requests exceeding this size are rejected with 413 Payload Too Large.
     */
    std::size_t max_body_size{10UZ * 1024UZ * 1024UZ};

    /**
     * @brief Per-request timeout. Default: 30 seconds.
     *
     * If a request is not fully received within this window, the connection is
     * closed. Enforced by the connection handler.
     */
    std::chrono::seconds request_timeout{30};
};

// =============================================================================
// App
// =============================================================================

/**
 * @brief Top-level Aevox framework entry point.
 *
 * `App` owns the `Router` (for dispatch) and the `Executor` (for async I/O).
 * Register routes via `get()`, `post()`, and related methods, then call
 * `listen()` to start accepting connections.
 *
 * Typical usage:
 * @code
 * int main() {
 *     aevox::App app;
 *     app.get("/hello", [](aevox::Request& req) {
 *         return aevox::Response::ok("Hello, World!");
 *     });
 *     app.listen(8080);  // blocks until SIGINT/SIGTERM
 * }
 * @endcode
 *
 * @note Thread-safety: Not thread-safe. All registration calls and `listen()`
 *       must originate from one thread (typically `main`). After `listen()`
 *       starts, worker threads call `Router::dispatch()` concurrently — that
 *       is safe per the `Router` contract.
 * @note Move-only. A moved-from `App` must not be used.
 * @note Ownership: `App` owns both `Router` and `Executor`. Neither should
 *       be kept alive past the `App` destructor.
 */
class App
{
public:
    /**
     * @brief Constructs App with the given configuration.
     *
     * Creates an empty Router and allocates the Executor (does not bind a
     * port yet). Port binding happens in `listen()`.
     *
     * @param config  Configuration. All fields have defaults.
     */
    explicit App(AppConfig config = {});

    /**
     * @brief Destructs App, stopping the executor if still running.
     *
     * Calls `Executor::stop()` and waits for the drain to complete before
     * destroying the Router and its handlers.
     *
     * @note Defined out-of-line because `App::Impl` is incomplete here.
     */
    ~App();

    /**
     * @brief Move constructor.
     *
     * @param other  App to move from. `other` must not be used after the move.
     */
    App(App&& other) noexcept;

    App(const App&)            = delete;
    App& operator=(const App&) = delete;
    App& operator=(App&&)      = delete;

    // -------------------------------------------------------------------------
    // Convenience route registration — forwarded to internal Router
    // -------------------------------------------------------------------------

    /**
     * @brief Registers a GET handler on the internal Router.
     *
     * Equivalent to `app.router().get(pattern, handler)`.
     *
     * @tparam Handler  Compatible handler callable. See `Router::get()`.
     * @param  pattern  Route pattern string.
     * @param  handler  Handler callable, stored by value.
     * @note   Not thread-safe. Must be called before `listen()`.
     * @note   `std::function` requires CopyConstructible captures (AEV-015).
     */
    template <typename Handler> void get(std::string_view pattern, Handler&& handler);

    /**
     * @brief Registers a POST handler on the internal Router.
     *
     * @tparam Handler  Compatible handler callable. See `Router::post()`.
     * @param  pattern  Route pattern string.
     * @param  handler  Handler callable, stored by value.
     * @note   Not thread-safe. Must be called before `listen()`.
     */
    template <typename Handler> void post(std::string_view pattern, Handler&& handler);

    /**
     * @brief Registers a PUT handler on the internal Router.
     *
     * @tparam Handler  Compatible handler callable. See `Router::put()`.
     * @param  pattern  Route pattern string.
     * @param  handler  Handler callable, stored by value.
     * @note   Not thread-safe. Must be called before `listen()`.
     */
    template <typename Handler> void put(std::string_view pattern, Handler&& handler);

    /**
     * @brief Registers a PATCH handler on the internal Router.
     *
     * @tparam Handler  Compatible handler callable. See `Router::patch()`.
     * @param  pattern  Route pattern string.
     * @param  handler  Handler callable, stored by value.
     * @note   Not thread-safe. Must be called before `listen()`.
     */
    template <typename Handler> void patch(std::string_view pattern, Handler&& handler);

    /**
     * @brief Registers a DELETE handler on the internal Router.
     *
     * @tparam Handler  Compatible handler callable. See `Router::del()`.
     * @param  pattern  Route pattern string.
     * @param  handler  Handler callable, stored by value.
     * @note   Not thread-safe. Must be called before `listen()`.
     */
    template <typename Handler> void del(std::string_view pattern, Handler&& handler);

    /**
     * @brief Registers an OPTIONS handler on the internal Router.
     *
     * @tparam Handler  Compatible handler callable. See `Router::options()`.
     * @param  pattern  Route pattern string.
     * @param  handler  Handler callable, stored by value.
     * @note   Not thread-safe. Must be called before `listen()`.
     */
    template <typename Handler> void options(std::string_view pattern, Handler&& handler);

    /**
     * @brief Installs a middleware function that wraps every handler.
     *
     * In v0.1 this is a no-op stub. Middleware integration is implemented in
     * AEV-008.
     *
     * @tparam Handler  Middleware callable. Signature defined in AEV-008.
     * @param  handler  Middleware callable (ignored in v0.1).
     */
    template <typename Handler>
    [[deprecated("use() is a v0.1 stub — implemented in AEV-008")]]
    void use(Handler&& handler);

    /**
     * @brief Returns a child Router with a shared path prefix.
     *
     * Forwards to the internal `Router::group()`. See `Router::group()` for
     * the full specification. In v0.1, group middleware is deferred to AEV-008.
     *
     * @param  prefix  Path prefix for the group. Must start with `/`.
     * @return         Child Router scoped to the prefix.
     * @note   Not thread-safe. Must be called before `listen()`.
     * @note   The returned Router's lifetime must not exceed this App's lifetime.
     */
    [[nodiscard]] Router group(std::string_view prefix);

    /**
     * @brief Returns a mutable reference to the internal Router.
     *
     * @return  Mutable reference to the owned Router.
     * @note    Valid for the lifetime of this App.
     */
    [[nodiscard]] Router& router() noexcept;

    /**
     * @brief Returns a const reference to the internal Router.
     *
     * @return  Const reference to the owned Router.
     */
    [[nodiscard]] const Router& router() const noexcept;

    // -------------------------------------------------------------------------
    // Server lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Binds to `port` and starts accepting connections.
     *
     * Blocks the calling thread until `stop()` is called (via SIGINT/SIGTERM
     * signal or an explicit `stop()` call from another thread).
     *
     * Sequence:
     * 1. Installs `SIGINT`/`SIGTERM` handlers that call `executor_->stop()`.
     * 2. Calls `executor_->listen(port, connection_handler)`.
     * 3. Calls `executor_->run()` — blocks here.
     * 4. On stop, drain completes and this call returns.
     *
     * @param  port  TCP port to bind. Overrides `AppConfig::port`.
     * @note   Blocks until the server stops. Call from `main()` only.
     * @note   Calling `listen()` more than once on the same App is undefined
     *         behaviour.
     */
    void listen(std::uint16_t port);

    /**
     * @brief Binds to `AppConfig::port` and starts accepting connections.
     *
     * Equivalent to `listen(config.port)`.
     *
     * @note   Blocks until the server stops.
     */
    void listen();

    /**
     * @brief Signals the executor to stop and begin draining.
     *
     * Thread-safe. Safe to call from a signal handler or any thread after
     * `listen()` has started.
     */
    void stop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aevox

// =============================================================================
// App template method bodies
//
// Defined here (after the App class and after router.hpp has been included above)
// so that aevox::App is a complete type and aevox::Router::get/post/... are already
// fully defined (via router.hpp → router_impl.hpp). Putting these in router_impl.hpp
// would fail because router.hpp includes router_impl.hpp before app.hpp declares App.
// =============================================================================

template <typename Handler> inline void aevox::App::get(std::string_view pattern, Handler&& handler)
{
    router().get(pattern, std::forward<Handler>(handler));
}

template <typename Handler>
inline void aevox::App::post(std::string_view pattern, Handler&& handler)
{
    router().post(pattern, std::forward<Handler>(handler));
}

template <typename Handler> inline void aevox::App::put(std::string_view pattern, Handler&& handler)
{
    router().put(pattern, std::forward<Handler>(handler));
}

template <typename Handler>
inline void aevox::App::patch(std::string_view pattern, Handler&& handler)
{
    router().patch(pattern, std::forward<Handler>(handler));
}

template <typename Handler> inline void aevox::App::del(std::string_view pattern, Handler&& handler)
{
    router().del(pattern, std::forward<Handler>(handler));
}

template <typename Handler>
inline void aevox::App::options(std::string_view pattern, Handler&& handler)
{
    router().options(pattern, std::forward<Handler>(handler));
}

template <typename Handler>
[[deprecated("use() is a v0.1 stub — implemented in AEV-008")]]
inline void aevox::App::use(Handler&& /*handler*/)
{
    // v0.1 stub — AEV-008 implements middleware composition.
}
