# Async Helpers

> Three primitives for concurrent and offloaded work in coroutine handlers: `pool`, `sleep`, and `when_all`.

## Overview

The async helpers in `<aevox/async.hpp>` extend the coroutine programming model beyond simple I/O suspension. They are designed to be used inside `aevox::Task<T>` coroutines running on an executor-managed thread.

All three helpers:
- Are **non-blocking** — they suspend the calling coroutine without occupying a thread.
- Are **Asio-free** — no Asio types appear in the public header.
- Are only valid on **executor-managed I/O threads** (i.e., inside a connection handler or a Task spawned from one).

These helpers are **not** for use in `main()`, raw `std::thread` functions, or any context outside an active executor.

## Quick Start

```cpp
#include <aevox/async.hpp>

// CPU-bound offload
auto result = co_await aevox::pool([] {
    return expensive_computation(); // runs on CPU pool, not I/O thread
});

// Non-blocking delay
co_await aevox::sleep(std::chrono::milliseconds{50});

// Concurrent fan-out
auto [orders, users] = co_await aevox::when_all(
    db.query<Order>("SELECT * FROM orders"),
    db.query<User>("SELECT * FROM users")
);
```

---

## API Reference

### `aevox::pool(fn)`

```cpp
template<std::invocable Fn>
[[nodiscard]] aevox::Task<std::invoke_result_t<Fn>>
aevox::pool(Fn&& fn);
```

Dispatches a CPU-bound callable to the dedicated CPU thread pool. The calling coroutine suspends until `fn` completes; the I/O thread is freed for other work during execution.

**Template parameters**

| Parameter | Constraint | Description |
|---|---|---|
| `Fn` | `std::invocable` | Callable to execute. Must be move-constructible. |

**Parameters**

| Name | Description |
|---|---|
| `fn` | Callable to execute on the CPU pool. Captured by move. |

**Returns** `Task<R>` where `R = std::invoke_result_t<Fn>`. When `R` is `void`, returns `Task<void>`.

**Exceptions** Any exception thrown by `fn` propagates through the task to the `co_await` site.

**Example:**

```cpp
aevox::Task<aevox::Response> handle_resize(aevox::Request& req) {
    auto body = co_await req.body();

    // image_resize() is CPU-intensive — offload it so the I/O thread
    // can continue accepting other requests.
    auto resized = co_await aevox::pool([&body] {
        return image_resize(body, 800, 600);
    });

    co_return aevox::Response::ok(resized).content_type("image/jpeg");
}
```

**CPU pool sizing:** Configured via `ExecutorConfig::cpu_pool_threads` (default 4).
When `cpu_pool_threads == 0`, `pool()` posts to the I/O pool instead (no separate CPU pool).

---

### `aevox::sleep(duration)`

```cpp
[[nodiscard]] aevox::Task<void>
aevox::sleep(std::chrono::steady_clock::duration duration);
```

Suspends the calling coroutine for at least `duration`. The I/O thread is freed during the wait — other coroutines may run.

**Parameters**

| Name | Description |
|---|---|
| `duration` | Minimum suspension time. Negative values are treated as zero. |

**Returns** `Task<void>` that completes after the duration elapses.

**Example:**

```cpp
aevox::Task<void> poll_sensor(std::uint64_t /*conn_id*/) {
    for (;;) {
        auto reading = read_sensor();
        // yield to other coroutines for 100ms between polls
        co_await aevox::sleep(std::chrono::milliseconds{100});
    }
}
```

**Note:** Resume time may be slightly later than `duration` due to I/O loop scheduling granularity. This is not a real-time guarantee.

---

### `aevox::when_all(tasks...)`

```cpp
template<typename... Ts>
    requires (sizeof...(Ts) >= 2)
          && (... && (!std::is_void_v<Ts>))
          && (... && std::movable<Ts>)
[[nodiscard]] aevox::Task<std::tuple<Ts...>>
aevox::when_all(aevox::Task<Ts>... tasks);
```

Runs multiple tasks concurrently and returns all results as a tuple. All tasks are posted to the I/O pool simultaneously; the calling coroutine suspends until every task completes.

**Template parameters**

| Parameter | Constraint | Description |
|---|---|---|
| `Ts...` | Non-void, `std::movable` | Result types of the input tasks. At least 2 required. |

**Parameters**

| Name | Description |
|---|---|
| `tasks...` | Pack of `Task<Ts>...` to run concurrently. |

**Returns** `Task<std::tuple<Ts...>>`. Tuple elements are in the same order as `tasks`.

**Error semantics (v0.1):** If any task throws, the first exception is re-thrown at the `co_await` site. Remaining tasks complete naturally (no cancellation). Structured cancellation is planned for v0.2.

**Constraints:**
- `sizeof...(Ts) >= 2` — single-task `when_all` is a compile error.
- `void` result types are not supported — use separate `co_await` for `Task<void>`.

**Example:**

```cpp
aevox::Task<aevox::Response> handle_report(aevox::Request& req) {
    // Both queries start simultaneously.
    auto [orders, users] = co_await aevox::when_all(
        db.query<Order>("SELECT * FROM orders"),
        db.query<User>("SELECT * FROM users")
    );

    auto report = co_await aevox::pool([&] {
        return generate_pdf_report(orders, users);
    });

    co_return aevox::Response::ok(report).content_type("application/pdf");
}
```

---

## Thread Safety

All three helpers are **only valid on executor-managed I/O threads**. The thread-local context required for their operation is initialised by `AsioExecutor` when each I/O thread starts.

Calling `pool()`, `sleep()`, or `when_all()` from:
- `main()` — **undefined behaviour**
- A raw `std::thread` — **undefined behaviour**
- A CPU pool thread (inside a `pool()` callable) — **undefined behaviour** for `pool()` and `sleep()`; do not nest them

In debug builds, an `assert` fires on misuse if `NDEBUG` is not defined.

---

## Performance Notes

| Helper | Overhead | Notes |
|---|---|---|
| `pool(fn)` | ~2 × `asio::post` + coroutine suspend/resume | Cross-pool post dominates for very short callables |
| `sleep(d)` | Asio `steady_timer` + coroutine suspend/resume | Timer precision is OS-dependent (~1ms granularity typical) |
| `when_all(...)` | N × `asio::post` + N coroutine launches + 1 shared_ptr allocation | Scales linearly with task count; shared_ptr allocation is amortized |

**CPU pool sizing guideline:** default 4 threads suits image resizing, PDF generation, and large JSON serialisation. For primarily I/O-bound workloads, `cpu_pool_threads = 0` reduces thread count with no correctness impact.

---

## See Also

- [Executor](executor.md) — `make_executor()`, `ExecutorConfig`, `Executor` interface
- [PRD §9.4](../architecture/index.md) — Thread pool + coroutine execution model design
