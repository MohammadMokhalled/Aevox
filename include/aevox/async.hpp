#pragma once
// include/aevox/async.hpp
//
// Async helper primitives for application-level coroutines.
//
//   pool(fn)          — offload CPU-bound work to the dedicated CPU thread pool.
//   sleep(duration)   — non-blocking timer suspension.
//   when_all(tasks...) — concurrent fan-out over multiple Task<T> values.
//
// All three are valid only when called from a coroutine running on an
// aevox::Executor-managed I/O thread. Behaviour is undefined if called
// from any other context (raw std::thread, main(), etc.).
//
// No Asio types appear in this file. The Asio binding is injected via three
// thread_local std::function bridges in aevox::detail (set by AsioExecutor
// when each I/O worker thread starts). Implementations live inline here
// because all public helpers are templates.
//
// Design: Tasks/architecture/AEV-006-arch.md §3, §4
// PRD §9.3, §9.4 — Execution model, async helpers

#include <aevox/task.hpp>

#include <atomic> // std::atomic — must precede task.hpp to avoid partial-template ODR issue
#include <cassert>
#include <chrono>
#include <concepts>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace aevox {

// =============================================================================
// aevox::detail — INTERNAL. Not part of the public API.
//
// The types in this namespace (FireAndForget, PoolAwaitable, SleepAwaitable,
// WhenAllState, WhenAllAwaitable, when_all_subtask, and the three tl_post_*
// thread-local bridges) are implementation details of pool(), sleep(), and
// when_all(). They are defined here — rather than in a separate .cpp — because
// C++ requires template definitions to be fully visible at every instantiation
// site. Do not name, specialise, or depend on these types directly; they may
// change or be removed in any release without notice.
// =============================================================================

// =============================================================================
// Thread-local executor bridges — declared here, defined in src/net/executor_context.cpp
//
// These are set by AsioExecutor for each I/O worker thread before io_ctx_.run().
// They must never be called from outside an executor-managed thread.
// =============================================================================

namespace detail {

/**
 * @brief Posts a callable to the CPU thread pool.
 *
 * Bound to `asio::post(cpu_pool_executor, fn)` by AsioExecutor at thread startup.
 * When cpu_pool_threads == 0, bound to `tl_post_to_io` instead.
 *
 * @note Valid only on executor I/O threads. std::function is empty on other threads.
 */
extern thread_local std::function<void(std::move_only_function<void()>)> tl_post_to_cpu;

/**
 * @brief Posts a callable to the I/O thread pool (for resuming coroutines).
 *
 * Bound to `asio::post(io_context_executor, fn)` by AsioExecutor at thread startup.
 * Accepts move-only callables (required for when_all sub-task lambdas that capture
 * move-only Task<T> values).
 *
 * @note Valid only on executor I/O threads. std::function is empty on other threads.
 */
extern thread_local std::function<void(std::move_only_function<void()>)> tl_post_to_io;

/**
 * @brief Schedules a callable after a duration using the I/O context's timer.
 *
 * Bound to an Asio `steady_timer` launcher by AsioExecutor at thread startup.
 *
 * @note Valid only on executor I/O threads. std::function is empty on other threads.
 */
extern thread_local std::function<void(std::chrono::steady_clock::duration,
                                       std::move_only_function<void()>)>
    tl_schedule_after;

// =============================================================================
// FireAndForget — internal coroutine driver (no await_transform restrictions)
//
// Starts immediately (suspend_never initial), self-destructs on completion
// (suspend_never final). Terminates on unhandled exception — callers must catch
// before the co_return point if they want to propagate errors.
// =============================================================================

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

// =============================================================================
// PoolAwaitable<Fn> — suspends caller, posts Fn to CPU pool, resumes on I/O pool
// =============================================================================

/**
 * @brief Internal awaitable for aevox::pool(fn).
 *
 * Lives on the pool() coroutine frame (compiler-allocated). All state is
 * stored inline — no extra heap allocation on the hot path.
 *
 * @tparam Fn  Callable type. Must be move-constructible. Return type R may be void.
 */
template <typename Fn> struct PoolAwaitable
{
    using R = std::invoke_result_t<Fn>;

    // Stored inline on the coroutine frame — no allocation.
    Fn                                                                      fn_;
    std::conditional_t<std::is_void_v<R>, std::monostate, std::optional<R>> result_{};
    std::exception_ptr                                                      exception_{};

    explicit PoolAwaitable(Fn&& fn) : fn_(std::forward<Fn>(fn)) {}

    // Never immediately ready — always suspends.
    [[nodiscard]] bool await_ready() const noexcept
    {
        return false;
    }

    /**
     * Suspends the calling coroutine and posts fn_ to the CPU pool.
     * When fn_ completes, the result/exception is stored and the caller
     * is resumed on the I/O pool via tl_post_to_io.
     *
     * Thread-safety: this pointer is captured; the coroutine frame (and thus
     * this awaitable) is valid for the entire duration — the caller is suspended
     * and cannot be destroyed until await_resume() returns.
     */
    void await_suspend(std::coroutine_handle<> caller)
    {
        assert(detail::tl_post_to_cpu && "pool() called outside an executor-managed thread");
        assert(detail::tl_post_to_io && "pool() called outside an executor-managed thread");

        detail::tl_post_to_cpu([this, caller, resume = detail::tl_post_to_io]() mutable {
            try {
                if constexpr (std::is_void_v<R>) {
                    std::invoke(fn_);
                }
                else {
                    result_.emplace(std::invoke(fn_));
                }
            }
            catch (...) {
                exception_ = std::current_exception();
            }
            // Resume caller on the I/O pool, not the CPU pool thread.
            // asio::post ensures happens-before between writes above and
            // the await_resume() read below.
            resume([caller]() mutable { caller.resume(); });
        });
    }

    /**
     * Returns the result of fn_ or rethrows any exception it threw.
     * Called on the resuming I/O thread — after CPU-thread writes are visible
     * via the asio::post sequencing in await_suspend.
     */
    R await_resume()
    {
        if (exception_)
            std::rethrow_exception(exception_);
        if constexpr (!std::is_void_v<R>)
            return std::move(*result_);
    }
};

// =============================================================================
// SleepAwaitable — suspends caller for a duration, resumes on I/O pool
// =============================================================================

/**
 * @brief Internal awaitable for aevox::sleep(duration).
 *
 * Posts a timer via tl_schedule_after; the timer fires on the I/O pool
 * and resumes the calling coroutine.
 */
struct SleepAwaitable
{
    std::chrono::steady_clock::duration duration_;

    explicit SleepAwaitable(std::chrono::steady_clock::duration d) : duration_{d} {}

    [[nodiscard]] bool await_ready() const noexcept
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> caller)
    {
        assert(detail::tl_schedule_after && "sleep() called outside an executor-managed thread");

        detail::tl_schedule_after(duration_, [caller]() mutable { caller.resume(); });
    }

    void await_resume() noexcept {}
};

// =============================================================================
// WhenAllState<Ts...> — shared state for when_all sub-tasks
// =============================================================================

/**
 * @brief Shared state between the when_all awaitable and all sub-task coroutines.
 *
 * Owned via std::shared_ptr. Lifetime: from WhenAllAwaitable::await_suspend()
 * until the last sub-task decrements remaining to zero and the continuation
 * resumes. The continuation reads results/exception via await_resume() before
 * any sub-task state is destroyed (shared_ptr refcount holds it alive).
 *
 * Thread-safety:
 *   - results: written by sub-tasks at distinct indices — no conflict.
 *   - first_exception: protected by exception_mutex — written at most once
 *     per task (only if it throws).
 *   - remaining: std::atomic, fetch_sub(acq_rel) establishes happens-before
 *     between all result writes and the await_resume() read.
 *   - continuation: written once before sub-tasks start; read once after
 *     the last sub-task decrements remaining.
 *
 * @tparam Ts  Non-void result types. One per sub-task.
 */
template <typename... Ts> struct WhenAllState
{
    std::tuple<std::optional<Ts>...> results;
    std::mutex                       exception_mutex;
    std::exception_ptr               first_exception;
    std::atomic<std::size_t>         remaining;
    std::coroutine_handle<>          continuation;

    explicit WhenAllState(std::size_t n) : remaining{n} {}
};

// =============================================================================
// when_all_subtask<I, Ts...> — FireAndForget coroutine for one sub-task
// =============================================================================

/**
 * @brief Drives one sub-task to completion and stores the result in shared state.
 *
 * Runs as a FireAndForget coroutine on the I/O pool. Decrements remaining;
 * if this is the last task, posts the outer continuation's resumption.
 *
 * @tparam I    Index of this sub-task in the WhenAllState tuple.
 * @tparam Ts   All result types (used to resolve tuple_element_t<I, Ts...>).
 */
template <std::size_t I, typename... Ts>
FireAndForget when_all_subtask(Task<std::tuple_element_t<I, std::tuple<Ts...>>> task,
                               std::shared_ptr<WhenAllState<Ts...>>             state)
{
    try {
        std::get<I>(state->results).emplace(co_await task);
    }
    catch (...) {
        std::lock_guard lock{state->exception_mutex};
        if (!state->first_exception)
            state->first_exception = std::current_exception();
    }

    // Decrement the counter. If this was the last sub-task (previous value == 1),
    // resume the outer when_all coroutine on the I/O pool.
    // acq_rel ordering: all writes to results/first_exception happen-before
    // the continuation read in await_resume() (which runs after resume).
    if (state->remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        assert(detail::tl_post_to_io && "when_all sub-task not on an executor-managed thread");
        auto cont = state->continuation;
        detail::tl_post_to_io([cont]() mutable { cont.resume(); });
    }
}

// =============================================================================
// WhenAllAwaitable<Ts...> — suspends outer coroutine, fans out all sub-tasks
// =============================================================================

/**
 * @brief Internal awaitable for aevox::when_all(tasks...).
 *
 * await_suspend: creates shared WhenAllState, spawns all sub-tasks via
 *   tl_post_to_io, stores caller as the continuation.
 * await_resume: assembles results tuple or rethrows the first captured exception.
 *
 * @tparam Ts  Non-void result types of the input tasks.
 */
template <typename... Ts> struct WhenAllAwaitable
{
    std::tuple<Task<Ts>...>              tasks_;
    std::shared_ptr<WhenAllState<Ts...>> state_;

    explicit WhenAllAwaitable(Task<Ts>... tasks) : tasks_{std::move(tasks)...} {}

    [[nodiscard]] bool await_ready() const noexcept
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> caller)
    {
        assert(detail::tl_post_to_io && "when_all() called outside an executor-managed thread");

        state_               = std::make_shared<WhenAllState<Ts...>>(sizeof...(Ts));
        state_->continuation = caller;

        // Spawn all sub-tasks. Each captures a copy of the shared_ptr so state
        // stays alive until the last sub-task completes.
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (detail::tl_post_to_io(
                 [state = state_, task = std::move(std::get<I>(tasks_))]() mutable {
                     when_all_subtask<I, Ts...>(std::move(task), std::move(state));
                 }),
             ...);
        }(std::index_sequence_for<Ts...>{});
    }

    /**
     * Assembles the results tuple. Called after all sub-tasks complete and the
     * atomic counter reached zero — all result writes are visible (acq_rel).
     *
     * @throws Whatever exception was first captured by a failing sub-task.
     */
    std::tuple<Ts...> await_resume()
    {
        if (state_->first_exception)
            std::rethrow_exception(state_->first_exception);

        return std::apply([](auto&... opts) { return std::make_tuple(std::move(*opts)...); },
                          state_->results);
    }
};

} // namespace detail

// =============================================================================
// pool() — CPU offload
// =============================================================================

/**
 * @brief Dispatches a CPU-bound callable to the dedicated CPU thread pool.
 *
 * Suspends the calling coroutine, posts `fn` to the CPU thread pool, and
 * resumes the coroutine on the I/O thread pool once `fn` returns. The I/O
 * thread that was running the coroutine is freed to handle other work during
 * the CPU-pool execution.
 *
 * If the executor was constructed with `cpu_pool_threads = 0`, the callable
 * is posted to the I/O pool instead (no separate CPU pool — still non-blocking
 * to the calling coroutine).
 *
 * **Usage:**
 * @code
 * auto resized = co_await aevox::pool([&body] {
 *     return image_resize(body, 800, 600); // pure CPU work
 * });
 * @endcode
 *
 * @tparam Fn  A callable type satisfying `std::invocable`. Must be
 *             move-constructible.
 * @param  fn  Callable to execute on the CPU pool. Captured by move.
 * @return     `Task<R>` where `R` is the return type of `fn`. Completes when
 *             `fn` returns. Any exception thrown by `fn` propagates through
 *             the task to the `co_await` site.
 *
 * @note Thread-safe — callable on any executor I/O thread.
 * @note The callable runs on a CPU pool thread, not an I/O thread. Do not
 *       perform async I/O inside `fn`.
 * @note Undefined behaviour if called from outside an executor-managed thread.
 */
template <std::invocable Fn> [[nodiscard]] Task<std::invoke_result_t<Fn>> pool(Fn&& fn)
{
    using R = std::invoke_result_t<Fn>;
    if constexpr (std::is_void_v<R>) {
        co_await detail::PoolAwaitable<std::decay_t<Fn>>{std::forward<Fn>(fn)};
    }
    else {
        co_return co_await detail::PoolAwaitable<std::decay_t<Fn>>{std::forward<Fn>(fn)};
    }
}

// =============================================================================
// sleep() — non-blocking timer
// =============================================================================

/**
 * @brief Suspends the calling coroutine for the given duration.
 *
 * The coroutine is resumed after at least `duration` has elapsed. No thread
 * is blocked during the wait — the I/O thread is free to process other work.
 *
 * **Usage:**
 * @code
 * co_await aevox::sleep(std::chrono::milliseconds{100});
 * @endcode
 *
 * @param duration  Minimum suspension time. Negative durations are treated as
 *                  zero (immediate re-schedule on the next I/O loop tick).
 * @return          `Task<void>` that completes after the duration elapses.
 *
 * @note Resume time may be slightly longer than `duration` due to scheduler
 *       granularity. Not a real-time guarantee.
 * @note Undefined behaviour if called from outside an executor-managed thread.
 */
[[nodiscard]] inline Task<void> sleep(std::chrono::steady_clock::duration duration)
{
    co_await detail::SleepAwaitable{duration};
}

// =============================================================================
// when_all() — concurrent fan-out
// =============================================================================

/**
 * @brief Runs multiple tasks concurrently and returns all results as a tuple.
 *
 * All tasks are spawned on the I/O thread pool simultaneously. The calling
 * coroutine suspends until every task completes. Results are collected into
 * a `std::tuple` in the same order as the input tasks.
 *
 * **Error semantics (v0.1):** If any task throws an exception, the first
 * exception encountered is re-thrown at the `co_await` site. Remaining tasks
 * are allowed to complete naturally (no cancellation). Structured cancellation
 * is deferred to v0.2.
 *
 * **Usage:**
 * @code
 * auto [orders, users] = co_await aevox::when_all(
 *     db.query<Order>("SELECT * FROM orders"),
 *     db.query<User>("SELECT * FROM users")
 * );
 * @endcode
 *
 * @tparam Ts  Result types of the input tasks. None may be void — use separate
 *             `co_await` statements for `Task<void>`. Each `T` must satisfy
 *             `std::movable`.
 * @param  tasks  Pack of `Task<Ts>...` to run concurrently. At least 2 required.
 * @return  `Task<std::tuple<Ts...>>` that completes when all input tasks complete.
 *          Tuple elements are in the same order as `tasks`.
 *
 * @note All tasks are posted to the I/O executor and may run on different threads.
 * @note Undefined behaviour if called from outside an executor-managed thread.
 */
template <typename... Ts>
    requires(sizeof...(Ts) >= 2) && (... && (!std::is_void_v<Ts>)) && (... && std::movable<Ts>)
[[nodiscard]] Task<std::tuple<Ts...>> when_all(Task<Ts>... tasks)
{
    co_return co_await detail::WhenAllAwaitable<Ts...>{std::move(tasks)...};
}

} // namespace aevox
