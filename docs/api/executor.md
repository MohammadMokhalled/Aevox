# Executor

> The async I/O execution layer — manages the thread pool, TCP acceptors, and coroutine dispatch. Every Aevox server starts here.

**Header:** `#include <aevox/executor.hpp>`
**Task:** AEV-001 | **ADD:** `Tasks/architecture/AEV-001-arch.md`

---

## Overview

`aevox::Executor` is the single interface between Aevox's high-level framework code and the underlying OS async I/O library (currently Asio). Application code and higher-level Aevox modules never interact with Asio directly — they go through this interface.

This design exists for one reason: when `std::net` standardizes (expected C++29), the entire networking backend can be swapped by replacing the Asio implementation with a `std::net` implementation — with **zero changes** to application code or the Aevox public API.

The Executor manages:
- A thread pool sized to `hardware_concurrency()` by default
- One or more TCP acceptor loops (one per `listen()` call)
- Coroutine dispatch — each accepted connection becomes a coroutine task on the pool

The Executor does **not** manage HTTP parsing, routing, or request handling. Those are higher-level concerns built on top of it.

---

## Quick Start

```cpp
#include <aevox/executor.hpp>
#include <print>

int main() {
    auto executor = aevox::make_executor(); // hardware_concurrency() threads

    auto result = executor->listen(8080, [](std::uint64_t conn_id) -> aevox::Task<void> {
        std::println("New connection: {}", conn_id);
        // read, parse, respond — higher-level code goes here
        co_return;
    });

    if (!result) {
        std::println(stderr, "Failed to listen: {}", aevox::to_string(result.error()));
        return 1;
    }

    executor->run(); // blocks until stop() is called
}
```

---

## API Reference

### `aevox::make_executor()`

```cpp
[[nodiscard]] std::unique_ptr<Executor>
make_executor(std::size_t thread_count = 0) noexcept;
```

Creates the default Asio-backed `Executor`.

**Parameters**

| Parameter | Description |
|---|---|
| `thread_count` | Worker thread count. `0` (default) uses `std::thread::hardware_concurrency()`. Pass an explicit value in tests or constrained environments. |

**Returns** an owning `unique_ptr<Executor>`. Never returns `nullptr`.

**Example:**
```cpp
auto executor = aevox::make_executor();        // production: all cores
auto test_exec = aevox::make_executor(2);      // tests: 2 threads
```

---

### `aevox::Executor`

Abstract interface implemented by the framework. Obtain via `make_executor()` — never construct directly.

#### `listen()`

```cpp
[[nodiscard]] virtual std::expected<void, ExecutorError>
listen(std::uint16_t port,
       std::move_only_function<Task<void>(std::uint64_t)> handler) = 0;
```

Binds to a TCP port and registers a connection handler. Must be called before `run()`. Call multiple times to listen on multiple ports.

**Parameters**

| Parameter | Description |
|---|---|
| `port` | TCP port (1–65535). Port `0` lets the OS choose (useful in tests). |
| `handler` | Called once per accepted connection. Receives a monotonically increasing `conn_id`. Must return `aevox::Task<void>`. |

**Returns** `std::expected<void, ExecutorError>`:

| Error | Condition |
|---|---|
| `ExecutorError::bind_failed` | Port in use, insufficient permissions, or invalid address |
| `ExecutorError::listen_failed` | `listen()` syscall failed |

**Example:**
```cpp
auto r = executor->listen(8080, my_handler);
if (!r) { /* handle r.error() */ }
```

#### `run()`

```cpp
[[nodiscard]] virtual std::expected<void, ExecutorError> run() = 0;
```

Starts all worker threads and runs the event loop. **Blocks** the calling thread until `stop()` is called and all in-flight coroutines complete.

| Error | Condition |
|---|---|
| `ExecutorError::already_running` | `run()` called while already running |

#### `stop()`

```cpp
virtual void stop() noexcept = 0;
```

Signals the executor to stop accepting new connections and drain in-flight coroutines. Thread-safe — safe to call from a signal handler or another thread. After `stop()` returns, `run()` will return.

#### `thread_count()`

```cpp
[[nodiscard]] virtual std::size_t thread_count() const noexcept = 0;
```

Returns the number of worker threads in the pool.

---

### `aevox::ExecutorError`

```cpp
enum class ExecutorError {
    bind_failed,
    listen_failed,
    accept_failed,
    already_running,
    not_running,
};
```

| Value | Meaning | How to handle |
|---|---|---|
| `bind_failed` | Port in use or no permission | Check port, retry with a different port, or run with elevated privileges |
| `listen_failed` | OS rejected the listen call | System resource issue — log and exit |
| `accept_failed` | Single accept failed | Logged internally, loop continues — not fatal |
| `already_running` | `run()` called twice | Application logic error — fix the call site |
| `not_running` | Operation on stopped executor | Application logic error — fix the call site |

```cpp
[[nodiscard]] std::string_view to_string(ExecutorError e) noexcept;
```

---

### `aevox::Task<T>`

```cpp
template<typename T = void>
class Task;
```

The coroutine return type for all async Aevox operations. Connection handlers, middleware, and route handlers all return `aevox::Task<T>`.

**Header:** `#include <aevox/task.hpp>` (included by `executor.hpp`)

```cpp
// void task — no return value
aevox::Task<void> my_handler(std::uint64_t conn_id) {
    co_return;
}

// valued task — returns a result
aevox::Task<int> compute() {
    co_return 42;
}

// awaiting a task
aevox::Task<void> outer() {
    int v = co_await compute(); // v == 42
    co_return;
}
```

**Thread-safety:** A `Task` must be awaited on the same executor strand it was created on, unless explicitly transferred.

**Move semantics:** A moved-from `Task` is empty — check `valid()` before awaiting.

---

## Thread Safety

| Operation | Thread-safe? |
|---|---|
| `make_executor()` | Yes |
| `listen()` | No — call before `run()` only |
| `run()` | No — call from one thread only |
| `stop()` | **Yes** — call from any thread or signal handler |
| `thread_count()` | Yes |

---

## Performance Notes

- TCP_NODELAY is enabled on all accepted sockets by default (Nagle's algorithm disabled). This is correct for request/response workloads where latency matters more than throughput of small writes.
- The thread pool uses work-stealing to balance load across cores. CPU-bound handlers distribute automatically.
- Each accepted connection becomes one coroutine. Coroutines are cheap — thousands can be in-flight simultaneously without OS thread overhead.
- The `conn_id` is a monotonically increasing `uint64_t`. At 10M connections/sec it wraps in ~58,000 years.

---

## See Also

- [API Overview](index.md) — all public modules
- [Architecture: Executor Model](../architecture/index.md)
- PRD §5.5 — Layered Architecture
- PRD §5.6 — Executor Abstraction
- PRD §9 — Thread Pool + Coroutine Execution Model
- ADD: `Tasks/architecture/AEV-001-arch.md`
