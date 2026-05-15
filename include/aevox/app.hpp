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
// Design: Tasks/architecture/AEV-004-arch.md §3.2

#include <aevox/config.hpp>
#include <aevox/executor.hpp>
#include <aevox/log.hpp>
#include <aevox/middleware.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/router.hpp>
#include <aevox/task.hpp>
#include <aevox/websocket_handler.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
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
    /** @brief TCP port to bind. Default: kDefaultPort (8080). */
    std::uint16_t port{kDefaultPort};

    /**
     * @brief Bind address. Default: kDefaultHost (`"0.0.0.0"`, all interfaces).
     *
     * Pass `"127.0.0.1"` for loopback-only. IPv6 is not supported in v0.1.
     */
    std::string host{std::string{kDefaultHost}};

    /** @brief TCP listen backlog depth. Default: kDefaultBacklog (1024). */
    int backlog{kDefaultBacklog};

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
     * @brief Maximum allowed request body size in bytes.
     * Default: kDefaultMaxBodySize (10 MiB = 10,485,760 bytes).
     *
     * Requests exceeding this size are rejected with HTTP 413.
     *
     * @note Valid range: 1 byte to 2 GiB.
     */
    std::size_t max_body_size{kDefaultMaxBodySize};

    /**
     * @brief Per-request read timeout.
     * Default: kDefaultRequestTimeout (30 seconds).
     *
     * If a request is not fully received within this window the connection is
     * closed.
     *
     * @note Valid range: 1 second to 3600 seconds.
     */
    std::chrono::seconds request_timeout{kDefaultRequestTimeout};

    /**
     * @brief Maximum number of HTTP headers per request.
     * Default: kDefaultMaxHeaderCount (100).
     *
     * Requests with more headers are rejected with HTTP 431. Plumbed through
     * to `detail::ParserConfig::max_header_count` at connection handler
     * construction time.
     *
     * @note Valid range: 1 to 1000.
     */
    std::size_t max_header_count{kDefaultMaxHeaderCount};

    /**
     * @brief Maximum bytes read from the TCP socket in one read() call.
     * Default: kDefaultMaxReadBytes (65536 bytes).
     *
     * Passed as the `max_bytes` argument to `TcpStream::read()` in the
     * connection handler loop. Increasing reduces syscall frequency for large
     * requests; decreasing reduces per-connection memory pressure.
     *
     * @note Valid range: 512 bytes to 16 MiB.
     */
    std::size_t max_read_bytes{kDefaultMaxReadBytes};

    /**
     * @brief Logging subsystem configuration.
     * Default: INFO level, one ConsoleSink with Pretty format and colour.
     */
    LogConfig logging{};
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
     * @brief Factory that constructs App and optionally loads a TOML config file.
     *
     * When `config_path` is supplied and non-empty, the factory reads the file,
     * parses it as TOML, validates each field, and merges overrides into
     * `base_config` before constructing the App. Fields absent from the file
     * retain their values from `base_config`. Unrecognised TOML keys are silently
     * ignored and a warning is written to `std::clog`.
     *
     * When `config_path` is `std::nullopt` or empty, no file I/O is performed and
     * `base_config` is used as-is — identical to calling `App(base_config)` directly.
     *
     * @param base_config  Starting configuration. All fields have constexpr
     *                     defaults — pass `AppConfig{}` for pure-file-driven config.
     * @param config_path  Optional filesystem path to a TOML config file.
     * @return  `std::expected<App, ConfigErrorDetail>`:
     *          - `App` on success.
     *          - `ConfigErrorDetail` with `ConfigError::file_not_found` if the path
     *            does not exist on the filesystem.
     *          - `ConfigErrorDetail` with `ConfigError::parse_error` if the file
     *            contains invalid TOML.
     *          - `ConfigErrorDetail` with `ConfigError::invalid_value` and the
     *            offending key in `ConfigErrorDetail::key` if a field fails a range
     *            check.
     *
     * @note `[[nodiscard]]` — discarding the expected silently swallows config errors.
     * @note Not thread-safe. Must be called from the main thread before `listen()`.
     */
    [[nodiscard]] static std::expected<App, ConfigErrorDetail> create(
        AppConfig                       base_config = {},
        std::optional<std::string_view> config_path = std::nullopt) noexcept;

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
     * @note   `std::function` requires CopyConstructible captures (see handler_wrap.hpp).
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
     * @brief Registers a WebSocket handler on the given path pattern.
     *
     * When an HTTP request matching `path_pattern` carries the WebSocket
     * upgrade headers, Aevox performs the RFC 6455 handshake automatically,
     * constructs a `WebSocket` handle, and invokes `handler.on_open`.
     * Subsequent frames are delivered to `handler.on_message`. When the
     * connection closes, `handler.on_close` is invoked.
     *
     * The path pattern follows the same typed-segment syntax as HTTP routes:
     * - `/chat/{room}` — single named segment; extracted via `ws.topic()`
     *   (the first segment value is stored as the topic automatically).
     * - `/ws` — static path.
     * - `/stream/{id}` — named segment.
     *
     * If the request reaches the `ws()` handler but is not a valid WebSocket
     * upgrade (i.e. `req.is_websocket_upgrade()` is `false`), the framework
     * responds with HTTP 400 automatically — the `WebSocketHandler` callbacks
     * are never called.
     *
     * @param path_pattern  Route pattern (same syntax as `get()`, `post()`, etc.).
     * @param handler       Callback aggregate. Stored by move — the App owns it.
     *
     * @note Not thread-safe. Must be called before `listen()`.
     * @note Path parameters are accessible via `ws.topic()` for the first
     *       segment, and via the `WebSocket&` in `on_open`.
     */
    void ws(std::string_view path_pattern, WebSocketHandler handler);

    /**
     * @brief Registers a global middleware that wraps every route handler.
     *
     * Global middleware runs for all requests in onion order: if middlewares
     * A, B, C are registered, they execute A → B → C → handler → C → B → A.
     * The middleware callable must accept `(Request&, next_callable)` and
     * return `Task<Response>`. It may call `co_await next(req)` to continue
     * the chain or return its own response to short-circuit.
     *
     * @tparam F  Middleware callable. Must satisfy:
     *            `(Request&, std::move_only_function<Task<Response>(Request&)>) -> Task<Response>`
     * @param  mw  Middleware callable, stored by value.
     *
     * @note Not thread-safe. Must be called before `listen()`.
     * @note Middleware are applied in the order registered.
     *
     * Example:
     * @code
     * app.use([](aevox::Request& req, auto next) -> aevox::Task<aevox::Response> {
     *     req.log.debug("Before handler");
     *     auto res = co_await next(req);
     *     res.header("X-Custom", "value");
     *     co_return res;
     * });
     * @endcode
     */
    template <typename F>
        requires MiddlewareFn<F>
    inline void use(F&& mw)
    {
        use_impl(Middleware(std::forward<F>(mw)));
    }

    /**
     * @brief Registers route-scoped middleware for a path prefix.
     *
     * Scoped middleware runs only for requests whose path starts with `prefix`.
     * Scoped middleware wraps global middleware in the execution order:
     * global A → global B → scoped (for prefix) → handler → scoped → global B → global A.
     *
     * @tparam F  Middleware callable. Same signature as global `use()`.
     * @param  prefix  Path prefix (e.g., `/api`). Must start with `/`.
     * @param  mw      Middleware callable, stored by value.
     *
     * @note Not thread-safe. Must be called before `listen()`.
     * @note Multiple scoped middleware on the same prefix run in registration order.
     */
    template <typename F>
        requires MiddlewareFn<F>
    inline void use(std::string_view prefix, F&& mw)
    {
        use_impl(prefix, Middleware(std::forward<F>(mw)));
    }

    /**
     * @brief Returns a child Router with a shared path prefix.
     *
     * Forwards to the internal `Router::group()`. See `Router::group()` for
     * the full specification. In v0.1, group middleware is deferred.
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

    /**
     * @brief Returns the resolved configuration used by this App.
     *
     * Returns the AppConfig that was either passed to the constructor or merged
     * from a config file by `App::create()`. Useful for inspecting which values
     * are in effect (e.g. after config file loading).
     *
     * @return  Const reference to the AppConfig stored in the App's implementation.
     *          Valid for the lifetime of this App.
     *
     * @note Thread-safety: safe to call concurrently after construction completes.
     *       The returned config is immutable after construction.
     */
    [[nodiscard]] const AppConfig& config() const noexcept;

private:
    void use_impl(Middleware mw);
    void use_impl(std::string_view prefix, Middleware mw);

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

// Template implementations defined in app_impl.cpp
// (see end of app_impl.cpp for App::use<F> and App::use(prefix, F) definitions)
