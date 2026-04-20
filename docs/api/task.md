# Task

> The coroutine return type for all async Aevox operations — lazy, move-only, stack-safe.

**Header:** `#include <aevox/task.hpp>`
**Included by:** `<aevox/executor.hpp>` and `<aevox/async.hpp>` transitively
**ADD:** `Tasks/architecture/AEV-001-arch.md`

---

## Overview

`aevox::Task<T>` is the single coroutine return type used throughout the framework and exposed to application handlers. It is defined entirely in terms of the C++ standard library — no Asio types appear in `task.hpp`. This is a deliberate design constraint: when `std::net` standardizes (C++29), the networking backend can be swapped without touching any `Task<T>` code.

**Two variants:**

| Type | Use |
|---|---|
| `aevox::Task<T>` | Async operation that produces a value of type `T` |
| `aevox::Task<void>` | Async operation that produces no value (most handlers, middleware) |

Both variants are `[[nodiscard]]`. Discarding a `Task` without `co_await`-ing it silently drops the coroutine — the compiler will warn.

---

## Quick Start

```cpp
#include <aevox/task.hpp>

// A Task<void> — the type used for connection handlers
aevox::Task<void> handle_connection(std::uint64_t conn_id) {
    co_return;
}

// A Task<T> — produces a value
aevox::Task<int> compute() {
    co_return 42;
}

// Chain tasks with co_await
aevox::Task<void> outer(std::uint64_t conn_id) {
    int result = co_await compute(); // result == 42
    co_return;
}
```

---

## API Reference

### `aevox::Task<T>`

```cpp
template <typename T>
class [[nodiscard]] Task;
```

Coroutine return type for async operations that produce a value. `T` must not be `void` — use `Task<void>` (the explicit specialisation below) for no-value operations.

#### Construction

`Task<T>` objects are produced by the compiler when a coroutine function's return type is `Task<T>`. They are **not** constructed directly by user code.

```cpp
aevox::Task<std::string> read_line() {
    // ... async read ...
    co_return "hello";
}
```

#### Move semantics

`Task<T>` is **move-only**. Copying is deleted.

```cpp
auto t = read_line();      // t is valid
auto t2 = std::move(t);   // t2 is valid; t is now moved-from
// co_await t;             // undefined behaviour — t is moved-from
```

#### `valid()`

```cpp
[[nodiscard]] bool valid() const noexcept;
```

Returns `true` if the `Task` holds a live coroutine handle. Returns `false` after the `Task` has been moved-from.

Do not `co_await` a `Task` for which `valid()` returns `false`.

#### `co_await` interface

`Task<T>` satisfies the C++ awaitable concept. The `co_await` expression:
1. Suspends the calling coroutine.
2. Starts (or resumes) the `Task`'s coroutine body via symmetric transfer — no unbounded stack growth.
3. Returns the value produced by `co_return expr;` when the body completes.
4. Re-throws any exception that propagated out of the body.

```cpp
aevox::Task<void> example() {
    int v = co_await compute(); // suspends, runs compute(), returns 42
    co_return;
}
```

#### Laziness

The coroutine body does **not** start until the `Task` is `co_await`-ed. Destroying a `Task` before awaiting it destroys the coroutine frame without executing the body.

```cpp
{
    auto t = compute(); // body has NOT started yet
    // t destroyed here — body never runs
}
```

#### Exception propagation

If the coroutine body throws an unhandled exception, it is captured in the `Task`'s promise. The exception is re-thrown at the `co_await` site when `await_resume()` is called.

```cpp
aevox::Task<void> caller() {
    try {
        co_await risky_operation(); // exception re-thrown here
    }
    catch (const std::runtime_error& e) {
        // handle it
    }
}
```

---

### `aevox::Task<void>`

```cpp
template <>
class [[nodiscard]] Task<void>;
```

Explicit specialisation for coroutines that produce no value. Identical to `Task<T>` except:
- The coroutine body uses `co_return;` (no expression).
- `await_resume()` returns `void`.
- There is no result storage — exception propagation still works.

This is the type used for connection handlers, most middleware, and any fire-and-dispatch coroutine.

```cpp
aevox::Task<void> handle(std::uint64_t conn_id) {
    // ... do work ...
    co_return; // plain co_return — no value
}
```

All properties of `Task<T>` (lazy, move-only, `[[nodiscard]]`, symmetric transfer, exception propagation) apply identically.

---

## Key Properties

| Property | Detail |
|---|---|
| **Lazy** | The coroutine body does not start until `co_await`. Destroying before awaiting is safe and does not run the body. |
| **Move-only** | Copying is deleted. Moved-from `Task` has `valid() == false`; do not await. |
| **`[[nodiscard]]`** | Both `Task<T>` and `Task<void>` are marked `[[nodiscard]]`. Discarding without `co_await` triggers a compiler warning. |
| **Symmetric transfer** | `await_suspend` returns the inner coroutine handle via symmetric transfer. Deep `co_await` chains do not grow the call stack. |
| **No Asio exposure** | `task.hpp` includes only standard library headers. No Asio type appears anywhere. |
| **Exception propagation** | Exceptions thrown inside the body are captured and re-thrown at the `co_await` site. |

---

## Thread Safety

A `Task` must be `co_await`-ed on the same executor strand it was created on. Awaiting from multiple threads simultaneously is undefined behaviour.

`aevox::when_all()` handles multi-task fan-out safely — do not attempt to share a single `Task` handle across threads manually.

---

## Common Mistakes

**Discarding a Task without co_await:**
```cpp
// WRONG — silently drops the coroutine; compiler warns due to [[nodiscard]]
compute();

// CORRECT
co_await compute();
```

**Awaiting a moved-from Task:**
```cpp
auto t = compute();
auto t2 = std::move(t);
co_await t;  // WRONG — t.valid() == false, undefined behaviour
```

**Calling a coroutine function outside an executor thread:**

`Task<T>` itself has no executor dependency, but the async helpers (`pool`, `sleep`, `when_all`) require an active executor. Connection handlers are always called on executor threads.

---

## See Also

- [Executor](executor.md) — `make_executor()`, `Executor` interface, `ExecutorConfig`
- [Async Helpers](async.md) — `pool()`, `sleep()`, `when_all()`
- [API Overview](index.md)
- PRD §5.6 — Executor Abstraction
- ADD: `Tasks/architecture/AEV-001-arch.md` (internal reference)
