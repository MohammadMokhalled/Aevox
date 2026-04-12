#pragma once
// include/aevox/task.hpp
//
// Public coroutine return type for all async Aevox operations.
// No Asio types appear in this file — Task<T> is defined entirely in terms of
// the C++ standard library. The Asio integration happens inside src/net/ via a
// thin wrapper lambda that co_awaits Task<void> inside an asio::awaitable<void>.
//
// Design: ADD §3, §7 (AEV-001-arch.md Rev.2)

#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace aevox {

// Forward declaration — required by FinalAwaitable template.
template<typename T = void>
class Task;

namespace detail {

/**
 * @brief Awaitable returned by Task promise_type::final_suspend.
 *
 * Uses symmetric transfer so that resuming the waiting coroutine (the
 * continuation) happens via a compiler tail-call rather than a direct call.
 * This keeps the call stack bounded even in deep coroutine chains.
 *
 * @note Not part of the public Aevox API. Used internally by Task<T>.
 */
struct FinalAwaitable {
    /**
     * @brief Never immediately ready — always suspends to run the continuation.
     */
    [[nodiscard]] bool await_ready() const noexcept { return false; }

    /**
     * @brief Symmetrically transfers control to the waiting coroutine.
     *
     * @tparam P  The promise type of the completing coroutine (deduced).
     * @param  h  The coroutine handle of the Task that just completed.
     * @return    The continuation handle if one is registered, otherwise
     *            `std::noop_coroutine()` (Task was detached or at root).
     */
    template<typename P>
    [[nodiscard]] std::coroutine_handle<>
    await_suspend(std::coroutine_handle<P> h) noexcept {
        auto cont = h.promise().continuation_;
        return cont ? cont : std::noop_coroutine();
    }

    void await_resume() noexcept {}
};

} // namespace detail

// =============================================================================
// Task<T> — general template for non-void result types
// =============================================================================

/**
 * @brief Coroutine return type for all async Aevox operations that produce a value.
 *
 * `aevox::Task<T>` is the public coroutine type used throughout the framework
 * and exposed to application handlers. It is defined entirely in terms of the
 * C++ standard library — no Asio types appear in this header.
 *
 * @code
 * aevox::Task<int> compute() {
 *     co_return 42;
 * }
 *
 * aevox::Task<void> handler(std::uint64_t conn_id) {
 *     int result = co_await compute();
 *     // use result...
 *     co_return;
 * }
 * @endcode
 *
 * @tparam T  The value type produced when the coroutine completes.
 *            Must not be void — use `Task<void>` (explicit specialisation) instead.
 *
 * @note Thread-safety: A Task must be awaited on the same executor strand it
 *       was created on. Awaiting from multiple threads simultaneously is
 *       undefined behaviour.
 * @note Move semantics: A moved-from Task has `valid() == false` and must not
 *       be awaited. The destructor is safe to call on a moved-from Task.
 * @note Lifetime: Task is lazy — the coroutine body does not start until the
 *       Task is co_await-ed. Destroying a Task before awaiting it destroys the
 *       coroutine frame without executing the body.
 */
template<typename T>
class Task {
public:
    // =========================================================================
    // promise_type
    // Fully defined in this header — the compiler requires the complete
    // definition at every co_return / co_await call site. Contains only
    // standard library types; Asio never appears here.
    // =========================================================================

    /**
     * @brief Coroutine promise type for Task<T>.
     *
     * Manages the coroutine's result, exception, and continuation chain.
     * Not intended for direct use — the compiler generates the call sites.
     */
    class promise_type {
    public:
        /**
         * @brief Creates the Task object from the promise.
         *
         * Called by the compiler at coroutine entry before the body runs.
         */
        [[nodiscard]] Task get_return_object() noexcept {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        /**
         * @brief Lazy start — the coroutine body does not run until co_await.
         *
         * Suspending at the start ensures the continuation is registered
         * before the body executes, preventing any ordering races.
         */
        [[nodiscard]] std::suspend_always initial_suspend() noexcept { return {}; }

        /**
         * @brief Symmetric transfer to the waiting coroutine on completion.
         *
         * FinalAwaitable::await_suspend tail-calls the continuation handle,
         * keeping the stack depth bounded.
         */
        [[nodiscard]] detail::FinalAwaitable final_suspend() noexcept { return {}; }

        /**
         * @brief Stores the return value produced by `co_return expr;`.
         *
         * Only present (via `requires`) when T is not void.
         *
         * @param value  The value to store for retrieval by await_resume().
         */
        void return_value(T value) noexcept
            requires (!std::is_void_v<T>)
        {
            result_.emplace(std::move(value));
        }

        /**
         * @brief Captures the current exception for re-throw at the co_await site.
         *
         * Called by the compiler if an exception propagates out of the
         * coroutine body. The exception is re-thrown by await_resume().
         */
        void unhandled_exception() noexcept {
            exception_ = std::current_exception();
        }

        // Accessed by FinalAwaitable::await_suspend and Task::await_suspend.
        std::coroutine_handle<> continuation_{};

    private:
        friend class Task<T>;
        std::optional<T>   result_{};
        std::exception_ptr exception_{};
    };

    // =========================================================================
    // Construction and ownership
    // =========================================================================

    explicit Task(std::coroutine_handle<promise_type> h) noexcept : handle_{h} {}

    Task(Task&& other) noexcept
        : handle_{std::exchange(other.handle_, {})} {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;

    ~Task() {
        if (handle_) handle_.destroy();
    }

    /**
     * @brief Returns true if this Task holds a live coroutine handle.
     *
     * Returns false after the Task has been moved from or after the coroutine
     * has completed and the handle has been destroyed.
     */
    [[nodiscard]] bool valid() const noexcept {
        return static_cast<bool>(handle_);
    }

    // =========================================================================
    // Awaitable interface — enables `co_await task;`
    // =========================================================================

    /**
     * @brief Task is never immediately ready — always suspends the caller.
     *
     * The coroutine body starts (for the first time) only when await_suspend
     * is called. Subsequent awaits resume a suspended body.
     */
    [[nodiscard]] bool await_ready() const noexcept { return false; }

    /**
     * @brief Registers the caller as the continuation and starts this Task.
     *
     * Symmetric transfer: returns this Task's coroutine handle so the compiler
     * tail-calls into it, keeping the call stack depth bounded.
     *
     * @param caller  The coroutine handle of the co_await-ing coroutine.
     * @return        This Task's coroutine handle (symmetric transfer).
     */
    [[nodiscard]] std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation_ = caller;
        return handle_; // symmetric transfer — compiler tail-calls this
    }

    /**
     * @brief Returns the coroutine result or re-throws any stored exception.
     *
     * Called by the compiler when the co_await expression completes.
     *
     * @return  The value produced by `co_return expr;`.
     * @throws  Whatever exception was thrown inside the coroutine body and
     *          captured by unhandled_exception().
     */
    T await_resume() {
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
        return std::move(*handle_.promise().result_);
    }

private:
    std::coroutine_handle<promise_type> handle_{};
};

// =============================================================================
// Task<void> — explicit partial specialisation for fire-and-forget coroutines
// =============================================================================

/**
 * @brief Coroutine return type for async Aevox operations that produce no value.
 *
 * Identical to `Task<T>` except that `return_void()` replaces `return_value()`,
 * and `await_resume()` returns void. This is the type used by connection handlers
 * and most middleware functions.
 *
 * @code
 * aevox::Task<void> handle(std::uint64_t conn_id) {
 *     // ... handle connection ...
 *     co_return;
 * }
 * @endcode
 *
 * @note Thread-safety: same as Task<T> — await on the same strand as creation.
 * @note Move semantics: moved-from Task<void> has valid() == false.
 */
template<>
class Task<void> {
public:
    /** @brief Coroutine promise type for Task<void>. */
    class promise_type {
    public:
        [[nodiscard]] Task<void> get_return_object() noexcept {
            return Task<void>{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        [[nodiscard]] std::suspend_always initial_suspend() noexcept { return {}; }
        [[nodiscard]] detail::FinalAwaitable final_suspend() noexcept { return {}; }

        /** @brief Called when the coroutine reaches `co_return;`. */
        void return_void() noexcept {}

        /** @brief Captures exception for re-throw at co_await site. */
        void unhandled_exception() noexcept {
            exception_ = std::current_exception();
        }

        std::coroutine_handle<> continuation_{};

    private:
        friend class Task<void>;
        std::exception_ptr exception_{};
    };

    explicit Task(std::coroutine_handle<promise_type> h) noexcept : handle_{h} {}

    Task(Task&& other) noexcept
        : handle_{std::exchange(other.handle_, {})} {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    Task(const Task&)            = delete;
    Task& operator=(const Task&) = delete;

    ~Task() {
        if (handle_) handle_.destroy();
    }

    /** @brief Returns true if this Task holds a live coroutine handle. */
    [[nodiscard]] bool valid() const noexcept {
        return static_cast<bool>(handle_);
    }

    [[nodiscard]] bool await_ready() const noexcept { return false; }

    [[nodiscard]] std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation_ = caller;
        return handle_;
    }

    /**
     * @brief Returns void or re-throws any stored exception.
     *
     * @throws  Whatever exception was thrown inside the coroutine body.
     */
    void await_resume() {
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
    }

private:
    std::coroutine_handle<promise_type> handle_{};
};

} // namespace aevox
