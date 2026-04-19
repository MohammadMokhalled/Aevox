#pragma once
// include/aevox/router.hpp
//
// Public aevox::Router class — HTTP method + path pattern router.
//
// Thread-safety:
//   Route registration (get/post/put/patch/del/options/group) is NOT thread-safe.
//   All registration calls must complete on a single thread before the first call
//   to dispatch() or App::listen(). This is enforced by convention, not by a lock.
//
//   dispatch() IS thread-safe. Concurrent calls from any number of worker threads
//   are safe after registration is complete. The trie is read-only after the last
//   registration call.
//
// Move semantics:
//   Move-only. A moved-from Router has valid() == false. Calling any method on a
//   moved-from Router (other than the destructor) is undefined behaviour.
//
// Ownership:
//   Router owns all registered handlers by value (stored in the trie nodes).
//   Lambdas captured by reference must outlive the Router.
//
// Design: AEV-004-arch.md §3.1

#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/task.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace aevox::detail { struct Segment; } // forward declaration — defined in handler_wrap.hpp

namespace aevox {

// =============================================================================
// RouteError
// =============================================================================

/**
 * @brief Error codes produced by the Router dispatch path.
 *
 * These error codes correspond to the HTTP error responses synthesised by
 * `Router::dispatch()` when no handler is found or when a parameter conversion
 * fails. The actual response is always a complete `Response` object (404, 405,
 * or 400); this enum exists for test introspection.
 */
enum class RouteError : std::uint8_t
{
    NotFound,          ///< No route matched the request path for any method (→ 404).
    MethodNotAllowed,  ///< A route matched the path but not the HTTP method (→ 405).
    BadParam,          ///< A typed parameter failed its type conversion (→ 400).
};

// =============================================================================
// Router
// =============================================================================

/**
 * @brief HTTP method + path pattern router using a radix trie.
 *
 * `Router` maps incoming HTTP requests to handlers registered with
 * `get()`, `post()`, `put()`, `patch()`, `del()`, and `options()`.
 * Internally it uses a compact radix trie for O(depth) lookup.
 *
 * Three segment types are supported:
 * - **Static** — literal segments (`/users`, `/api/v1`).
 * - **Named parameter** — `{name}` (string) or `{name:int}`, `{name:uint}`,
 *   `{name:float}`, `{name:double}` (typed, converted at dispatch time).
 * - **Wildcard** — `{name...}` greedy tail capture (always the last segment).
 *
 * Regex routing is disabled in v0.1 per ADR-4.
 *
 * **Registration** is not thread-safe. All `get/post/…` calls must
 * complete before the first call to `dispatch()`.
 *
 * **Dispatch** is thread-safe. After registration is frozen, concurrent
 * `dispatch()` calls from any number of worker threads are safe.
 *
 * @note Move-only. A moved-from Router must not be used.
 * @note Handlers stored by value. Lambda captures must remain valid for the
 *       Router's lifetime.
 * @note `std::function` requires CopyConstructible captures. Handlers capturing
 *       move-only types (e.g. `std::unique_ptr`) must wrap them in
 *       `std::shared_ptr`. This limitation is tracked in AEV-015.
 */
class Router
{
public:
    /**
     * @brief Constructs an empty Router with no routes.
     *
     * @note Defined out-of-line in `router_impl.cpp` because `Router::Impl`
     *       is an incomplete type in this header.
     */
    Router();

    /**
     * @brief Destructs the Router, releasing all trie nodes and stored handlers.
     *
     * @note Defined out-of-line because `Router::Impl` is incomplete here.
     */
    ~Router();

    /**
     * @brief Move constructor — transfers trie ownership.
     *
     * @param other  Router to move from. After the move `other.valid() == false`.
     * @note Defined out-of-line because `Router::Impl` is incomplete here.
     */
    Router(Router&& other) noexcept;

    /**
     * @brief Move assignment — transfers trie ownership.
     *
     * @param other  Router to move from. After the move `other.valid() == false`.
     * @return       Reference to this Router.
     * @note Defined out-of-line because `Router::Impl` is incomplete here.
     */
    Router& operator=(Router&& other) noexcept;

    Router(const Router&)            = delete;
    Router& operator=(const Router&) = delete;

    /**
     * @brief Returns `true` if this Router holds a live trie.
     *
     * Returns `false` after the Router has been moved from. All other methods
     * (except the destructor) have undefined behaviour on a Router where
     * `valid() == false`.
     *
     * @return `true` if the Router's Impl is non-null.
     */
    [[nodiscard]] bool valid() const noexcept;

    // -------------------------------------------------------------------------
    // Route registration — call at startup only, before listen() / dispatch()
    // -------------------------------------------------------------------------

    /**
     * @brief Registers a GET handler for the given path pattern.
     *
     * The handler may be any callable satisfying one of these signatures:
     * - `(Request&) -> Response` — synchronous
     * - `(Request&) -> Task<Response>` — asynchronous
     * - `(Request&, T0) -> Response` — synchronous, one typed param
     * - `(Request&, T0) -> Task<Response>` — asynchronous, one typed param
     * - `(Request&, T0, T1) -> Response/Task<Response>` — two typed params
     *
     * `T0`, `T1` may each be `int`, `unsigned int`, `float`, `double`, or
     * `std::string`. They correspond to the named parameters in `pattern`,
     * left-to-right. Conversion failure at dispatch time returns a 400 response.
     *
     * @tparam Handler  Any callable matching the supported signatures above.
     * @param  pattern  Route pattern. Examples: `"/users"`, `"/users/{id:int}"`,
     *                  `"/files/{path...}"`. Must start with `/`.
     * @param  handler  Callable to invoke on match. Stored by value.
     * @note   Not thread-safe. Must be called before `dispatch()` or `listen()`.
     * @note   Duplicate registration (same method + pattern) silently overwrites.
     * @note   `std::function` requires CopyConstructible captures (AEV-015).
     */
    template <typename Handler>
    void get(std::string_view pattern, Handler&& handler);

    /**
     * @brief Registers a POST handler for the given path pattern.
     *
     * @tparam Handler  As documented for `get()`.
     * @param  pattern  Route pattern string.
     * @param  handler  Handler callable, stored by value.
     * @note   Not thread-safe. Must be called before `dispatch()`.
     * @note   `std::function` requires CopyConstructible captures (AEV-015).
     */
    template <typename Handler>
    void post(std::string_view pattern, Handler&& handler);

    /**
     * @brief Registers a PUT handler for the given path pattern.
     *
     * @tparam Handler  As documented for `get()`.
     * @param  pattern  Route pattern string.
     * @param  handler  Handler callable, stored by value.
     * @note   Not thread-safe. Must be called before `dispatch()`.
     * @note   `std::function` requires CopyConstructible captures (AEV-015).
     */
    template <typename Handler>
    void put(std::string_view pattern, Handler&& handler);

    /**
     * @brief Registers a PATCH handler for the given path pattern.
     *
     * @tparam Handler  As documented for `get()`.
     * @param  pattern  Route pattern string.
     * @param  handler  Handler callable, stored by value.
     * @note   Not thread-safe. Must be called before `dispatch()`.
     * @note   `std::function` requires CopyConstructible captures (AEV-015).
     */
    template <typename Handler>
    void patch(std::string_view pattern, Handler&& handler);

    /**
     * @brief Registers a DELETE handler for the given path pattern.
     *
     * @note `delete` is a reserved keyword in C++23. This method is named `del`.
     *
     * @tparam Handler  As documented for `get()`.
     * @param  pattern  Route pattern string.
     * @param  handler  Handler callable, stored by value.
     * @note   Not thread-safe. Must be called before `dispatch()`.
     * @note   `std::function` requires CopyConstructible captures (AEV-015).
     */
    template <typename Handler>
    void del(std::string_view pattern, Handler&& handler);

    /**
     * @brief Registers an OPTIONS handler for the given path pattern.
     *
     * @tparam Handler  As documented for `get()`.
     * @param  pattern  Route pattern string.
     * @param  handler  Handler callable, stored by value.
     * @note   Not thread-safe. Must be called before `dispatch()`.
     * @note   `std::function` requires CopyConstructible captures (AEV-015).
     */
    template <typename Handler>
    void options(std::string_view pattern, Handler&& handler);

    /**
     * @brief Creates a child Router with a shared path prefix.
     *
     * All routes registered on the returned Router are automatically prefixed
     * with `prefix`. The returned Router shares its trie root with the parent
     * through a prefix node — there is no separate memory allocation.
     *
     * In v0.1, middleware composition for groups is deferred to AEV-008.
     * `group()` provides prefix scoping only.
     *
     * @param  prefix  Path prefix for all routes in this group. Must start
     *                 with `/`. Example: `"/api/v1"`.
     * @return         Child Router scoped to the prefix.
     * @note   Not thread-safe. Must be called before `dispatch()`.
     * @note   **Lifetime:** The child Router holds a raw non-owning pointer into
     *         the parent's trie. The child must not outlive the parent Router.
     *         Moving the parent invalidates the child. Use child Routers only
     *         during the registration phase (before `listen()`).
     */
    [[nodiscard]] Router group(std::string_view prefix);

    /**
     * @brief Dispatches an HTTP request to the matching registered handler.
     *
     * Walks the radix trie using `req.path()` and `req.method()`. On a
     * successful match, injects captured path parameters into `req` (via
     * `friend class Router` access to `Request::Impl::params`) and invokes
     * the stored handler.
     *
     * Result semantics:
     * - **Match** — parameters injected, handler invoked, response returned.
     * - **Path match but method mismatch (405)** — returns 405 with
     *   `Allow` header listing registered methods for that path.
     * - **No path match (404)** — returns a 404 plain-text response.
     * - **Typed parameter conversion failure (400)** — returns 400 from
     *   within the normalised handler; the dispatch loop itself never errors.
     *
     * @param  req  The request to dispatch. Parameters are injected into `req`
     *              before the handler runs; the `Request` is modified in place.
     * @return      `Task<Response>` that always resolves. Sync handlers are
     *              wrapped in a trivially-completing coroutine.
     * @note   Thread-safe. Concurrent calls from multiple worker threads are
     *         safe after all registration calls have completed.
     */
    [[nodiscard]] aevox::Task<aevox::Response> dispatch(aevox::Request& req) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Constructor used by group() to create a child Router sharing the parent
    // trie but rooted at a different insert node.
    struct GroupTag {};
    explicit Router(GroupTag, std::unique_ptr<Impl> group_impl) noexcept;

    // Type-erased handler type. Matches aevox::detail::ErasedHandler in
    // handler_wrap.hpp. Defined here using only standard-library types to
    // avoid including internal headers in this public header.
    using ErasedHandler = std::function<Task<Response>(Request&)>;

    // Private non-template registration entry point. Defined in router_impl.cpp.
    // Called by the template methods below (via router_impl.hpp) with pre-parsed segments —
    // avoids re-parsing the pattern string a second time.
    void register_route(HttpMethod method,
                        std::span<const detail::Segment> segs,
                        ErasedHandler handler);
};

} // namespace aevox

// Template method bodies. This companion file includes src/router/handler_wrap.hpp
// (which must be on the include path). See AEV-004-arch.md §4.3.
#include <aevox/router_impl.hpp>
