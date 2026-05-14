/**
 * @file middleware.hpp
 * @brief Composable async middleware pipeline for request/response interception.
 *
 * Middleware are ordered, chainable units that intercept the request/response
 * lifecycle before and after route handlers run. A middleware may inspect or
 * modify the request, delegate to the next middleware via `co_await next(req)`,
 * and inspect or modify the returned response.
 *
 * @see PRD §6.5 — Middleware Model
 * @see ADD AEV-024 § Public API Design
 */

#ifndef AEVOX_MIDDLEWARE_HPP
#define AEVOX_MIDDLEWARE_HPP

#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/task.hpp>

#include <concepts>
#include <functional>
#include <string_view>

namespace aevox {

// Forward declarations
class App;

/**
 * @brief Concept constraining the "next" callable in a middleware chain.
 *
 * A `MiddlewareNext` callable takes a `Request&` and returns a `Task<Response>`.
 * It represents the remainder of the middleware chain, including the route handler.
 *
 * @tparam F Callable type to constrain.
 *
 * @note A middleware must call `co_await next(req)` to continue the chain,
 *       or return its own response to short-circuit.
 */
template <typename F>
concept MiddlewareNext = requires(F f, Request& req) {
    { f(req) } -> std::same_as<Task<Response>>;
};

/**
 * @brief Concept constraining a middleware callable.
 *
 * A `MiddlewareFn` takes a `Request&` and a `MiddlewareNext` callable,
 * and returns a `Task<Response>`. Each middleware is responsible for either
 * calling `co_await next(req)` to continue the chain or returning its own
 * response to short-circuit.
 *
 * @tparam F Middleware callable type.
 *
 * Deviation from ADD §3.2: The ADD specifies a two-parameter concept
 * `MiddlewareFn<F, Next>`. This implementation uses a single-parameter form
 * that hardcodes `std::move_only_function<Task<Response>(Request&)>` as the
 * concrete Next type. This is intentional: `std::move_only_function` is the
 * only Next type used anywhere in the pipeline; parameterising over Next would
 * add complexity with no benefit. Lambdas written as `[](Request& req, auto next)`
 * satisfy this concept because the concept instantiates `F` with the concrete
 * `std::move_only_function` type at concept-check time.
 *
 * Example:
 * ```cpp
 * auto logging_middleware = [](aevox::Request& req, auto next) -> aevox::Task<aevox::Response> {
 *     // Log request before forwarding
 *     auto res = co_await next(req);
 *     // Inspect response after handler
 *     co_return res;
 * };
 * ```
 *
 * @note Middleware execute in onion order: A → B → C → handler → C → B → A.
 *       All middleware must be `co_await`-able (return `Task<Response>`).
 */
template <typename F>
concept MiddlewareFn =
    requires(F fn, Request& req, std::move_only_function<Task<Response>(Request&)> next) {
        { fn(req, std::move(next)) } -> std::same_as<Task<Response>>;
    };

/**
 * @brief Type-erased middleware handle registered with `App::use()`.
 *
 * `Middleware` wraps an arbitrary callable satisfying the `MiddlewareFn` concept
 * using `std::move_only_function` for zero-cost type erasure (no virtual dispatch).
 * This enables application code to register middleware of any type without
 * exposing type erasure details or incurring vtable overhead.
 *
 * Constructed by `App::use()` — application code does not construct this directly.
 *
 * @note Move-only: copy construction and copy assignment are deleted.
 *       Moved-from state is invalid and must not be invoked.
 *
 * @note Thread-safe for concurrent invocations provided the stored callable
 *       has no shared mutable state. Each request receives its own context.
 *
 * @note Middleware must be registered before `App::listen()` is called.
 *       Calling `App::use()` after `listen()` is undefined behaviour.
 */
class Middleware
{
public:
    /**
     * @brief Invokes the middleware with a request and the next-chain callable.
     *
     * The middleware may call `co_await next(req)` to continue the chain,
     * or return its own response to short-circuit.
     *
     * @param req  Current HTTP request. May be mutated by the middleware.
     * @param next Callable representing the remainder of the chain.
     *             Signature: `Task<Response>(Request&)`.
     *
     * @return Awaitable task producing the final HTTP response.
     *
     * @note Non-const: `std::move_only_function::operator()` is not const-qualified
     *       by the standard. Declaring this operator const with a mutable member
     *       creates a misleading public contract (callers observing const would not
     *       expect state mutation on each call). Non-const is the correct design.
     */
    [[nodiscard]] Task<Response> operator()(Request&                                          req,
                                            std::move_only_function<Task<Response>(Request&)> next);

    // Delete copy operations; move operations are default.
    Middleware(const Middleware&)            = delete;
    Middleware& operator=(const Middleware&) = delete;

    Middleware(Middleware&&) noexcept            = default;
    Middleware& operator=(Middleware&&) noexcept = default;

    ~Middleware() = default;

    /**
     * @brief Constructs a Middleware from an arbitrary callable.
     *
     * The `requires MiddlewareFn<F>` constraint ensures the diagnostic is emitted
     * at the `Middleware(fn)` construction site, not deep inside
     * `std::move_only_function` template instantiation.
     *
     * @tparam F Callable type satisfying MiddlewareFn concept.
     * @param fn The middleware callable.
     */
    template <typename F>
        requires MiddlewareFn<F>
    explicit Middleware(F&& fn)
        : fn_(std::move_only_function<
              Task<Response>(Request&, std::move_only_function<Task<Response>(Request&)>)>(
              std::forward<F>(fn)))
    {}

private:
    friend class App;

    std::move_only_function<Task<Response>(Request&,
                                           std::move_only_function<Task<Response>(Request&)>)>
        fn_;
};

} // namespace aevox

#endif // AEVOX_MIDDLEWARE_HPP
