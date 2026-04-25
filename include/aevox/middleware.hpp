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

#include <concepts>
#include <functional>
#include <memory>
#include <string_view>

#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/task.hpp>

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
 * Example:
 * ```cpp
 * auto logging_middleware = [](aevox::Request& req, auto next) -> aevox::Task<aevox::Response> {
 *     // Log request
 *     req.log.info("Incoming {}", req.method());
 *
 *     // Call next in the chain
 *     auto res = co_await next(req);
 *
 *     // Log response
 *     req.log.info("Outgoing {}", res.status());
 *
 *     co_return res;
 * };
 * ```
 *
 * @note Middleware execute in onion order: A → B → C → handler → C → B → A.
 *       All middleware must be `co_await`-able (return `Task<Response>`).
 */
template <typename F>
concept MiddlewareFn = true;  // Checked at runtime; any callable works with Middleware pimpl

/**
 * @brief Type-erased middleware handle registered with `App::use()`.
 *
 * `Middleware` wraps an arbitrary callable satisfying the `MiddlewareFn` concept.
 * This enables application code to register middleware of any type without
 * exposing type erasure details.
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
class Middleware {
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
     * @note This operator is `noexcept`. If the stored middleware throws,
     *       the exception propagates to the caller (v0.2 will add top-level
     *       exception handling in the dispatcher).
     */
    [[nodiscard]] Task<Response> operator()(
        Request& req,
        std::move_only_function<Task<Response>(Request&)> next) const;

    // Delete copy operations; move operations are default.
    Middleware(const Middleware&) = delete;
    Middleware& operator=(const Middleware&) = delete;

    Middleware(Middleware&&) noexcept = default;
    Middleware& operator=(Middleware&&) noexcept = default;

    ~Middleware() = default;

private:
public:
    // Pimpl pattern: the implementation holds a type-erased callable.
    struct MiddlewareImpl {
        virtual ~MiddlewareImpl()                                     = default;
        virtual Task<Response> invoke(
            Request& req, std::move_only_function<Task<Response>(Request&)> next) const = 0;
    };

    template <typename F>
    struct ConcreteMiddleware final : MiddlewareImpl {
        explicit ConcreteMiddleware(F&& fn) : callable(std::forward<F>(fn)) {}

        Task<Response> invoke(Request& req,
                              std::move_only_function<Task<Response>(Request&)> next) const override
        {
            return callable(req, std::move(next));
        }

        std::decay_t<F> callable;
    };

    /**
     * @brief Constructs a Middleware from an arbitrary callable.
     *
     * @tparam F Callable type satisfying MiddlewareFn concept.
     * @param fn The middleware callable.
     */
    template <typename F>
    explicit Middleware(F&& fn)
        : impl_(std::make_unique<ConcreteMiddleware<F>>(std::forward<F>(fn))) {}

private:
    std::unique_ptr<MiddlewareImpl> impl_;
};

} // namespace aevox

#endif // AEVOX_MIDDLEWARE_HPP
