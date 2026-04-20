# Executor

> The async I/O execution layer — manages the thread pool, TCP acceptors, and coroutine dispatch. Every Aevox server starts here.

**Headers:** `#include <aevox/executor.hpp>` · `#include <aevox/task.hpp>` · `#include <aevox/tcp_stream.hpp>` (included transitively)

---

## Overview

`aevox::Executor` is the single interface between Aevox's high-level framework code and the underlying OS async I/O library (currently Asio). Application code and higher-level Aevox modules never interact with Asio directly — they go through this interface.

This design exists for one reason: when `std::net` standardizes (expected C++29), the entire networking backend can be swapped by replacing the Asio implementation with a `std::net` implementation — with **zero changes** to application code or the Aevox public API.

The Executor manages:

- A thread pool sized to `hardware_concurrency()` by default
- One or more TCP acceptor loops (one per `listen()` call)
- Coroutine dispatch — each accepted connection becomes a `Task<void>` coroutine on the pool
- Graceful drain — in-flight coroutines are given a configurable grace period on `stop()`

The Executor does **not** manage HTTP parsing, routing, or request handling. Those are higher-level concerns built on top of it.

---

## Quick Start

```cpp
#include <aevox/executor.hpp>
#include <aevox/tcp_stream.hpp>
#include <print>

int main() {
    auto executor = aevox::make_executor(); // hardware_concurrency() threads, 30s drain

    auto result = executor->listen(8080,
        [](std::uint64_t conn_id, aevox::TcpStream stream) -> aevox::Task<void> {
            std::println("New connection: {}", conn_id);
            // read, parse, respond — use stream.read() / stream.write()
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

### `aevox::ExecutorConfig`

```cpp
struct ExecutorConfig {
    std::size_t          thread_count{0};
    std::size_t          cpu_pool_threads{4};
    std::chrono::seconds drain_timeout{30};
};
```

Configuration for `make_executor()`. All fields have production-ready defaults.

| Field | Default | Description |
|---|---|---|
| `thread_count` | `0` | Worker thread count. `0` resolves to `std::max(1u, hardware_concurrency())`. |
| `cpu_pool_threads` | `4` | Thread count for the dedicated CPU thread pool used by `aevox::pool()`. Set to `0` to disable the dedicated CPU pool — `pool()` will post to the I/O pool instead. |
| `drain_timeout` | `30s` | Grace period after `stop()`. In-flight coroutines are given this long to finish; after expiry the pool is force-stopped. |

**Examples:**
```cpp
// Production defaults
auto ex = aevox::make_executor();

// Custom thread count only
auto ex = aevox::make_executor({.thread_count = 8});

// Large CPU pool for image-processing workloads
auto ex = aevox::make_executor({.cpu_pool_threads = 16});

// Test config: small pool, no CPU pool, short drain
auto ex = aevox::make_executor({.thread_count = 2,
                                 .cpu_pool_threads = 0,
                                 .drain_timeout = std::chrono::seconds{2}});
```

---

### `aevox::make_executor()`

```cpp
[[nodiscard]] std::unique_ptr<Executor>
make_executor(ExecutorConfig config = {}) noexcept;
```

Creates the default Asio-backed `Executor`. The concrete type is hidden in `src/net/` — callers only see `aevox::Executor`.

**Returns** an owning `unique_ptr<Executor>`. Never returns `nullptr` — failure to create OS threads calls `std::terminate` (unrecoverable).

---

### `aevox::Executor`

Abstract interface implemented by the framework. Obtain via `make_executor()` — never construct directly.

#### `listen()`

```cpp
[[nodiscard]] virtual std::expected<void, ExecutorError>
listen(std::uint16_t port,
       std::move_only_function<Task<void>(std::uint64_t, TcpStream)> handler) = 0;
```

Binds to a TCP port and registers a connection handler. Must be called before `run()`. May be called multiple times to listen on multiple ports.

**Parameters**

| Parameter | Description |
|---|---|
| `port` | TCP port (1–65535). Port `0` lets the OS choose an ephemeral port (useful in tests). |
| `handler` | Invoked once per accepted connection with a monotonically increasing `conn_id` and an owned `TcpStream`. The executor takes ownership of the handler. |

**Returns** `std::expected<void, ExecutorError>`:

| Error | Condition |
|---|---|
| `ExecutorError::bind_failed` | Port in use, insufficient permissions, or invalid address |
| `ExecutorError::listen_failed` | `listen()` syscall failed after successful bind |

**Example:**
```cpp
auto r = executor->listen(8080,
    [](std::uint64_t conn_id, aevox::TcpStream stream) -> aevox::Task<void> {
        // use stream.read() / stream.write() here
        co_return;
    });
if (!r) {
    std::println(stderr, "listen failed: {}", aevox::to_string(r.error()));
}
```

---

#### `run()`

```cpp
[[nodiscard]] virtual std::expected<void, ExecutorError> run() = 0;
```

Starts all worker threads and runs the event loop. **Blocks** the calling thread until `stop()` is called and the drain period completes.

| Error | Condition |
|---|---|
| `ExecutorError::already_running` | `run()` called while already running |

---

#### `stop()`

```cpp
virtual void stop() noexcept = 0;
```

Signals the executor to stop accepting new connections and begin draining.

Thread-safe — safe to call from a signal handler or any thread while `run()` is blocking.

**Drain sequence:**
1. All accept loops close (no new connections accepted).
2. In-flight coroutines are given `ExecutorConfig::drain_timeout` to finish.
3. If the timeout expires, the thread pool is force-stopped: remaining coroutines have pending I/O cancelled and frames destroyed.
4. `run()` returns.

Calling `stop()` before `run()` is a no-op. Calling it multiple times is safe.

---

#### `thread_count()`

```cpp
[[nodiscard]] virtual std::size_t thread_count() const noexcept = 0;
```

Returns the number of worker threads as resolved at construction (after `hardware_concurrency()` substitution). Always ≥ 1.

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

[[nodiscard]] std::string_view to_string(ExecutorError e) noexcept;
```

| Value | Meaning | How to handle |
|---|---|---|
| `bind_failed` | Port in use or no permission | Check port, try a different one, or run with elevated privileges |
| `listen_failed` | OS rejected the `listen()` call | System resource issue — log and exit |
| `accept_failed` | A single accept() failed | Logged internally, loop continues — not fatal to the server |
| `already_running` | `run()` called twice | Application logic error — fix the call site |
| `not_running` | Operation on stopped executor | Application logic error — fix the call site |

---

### `aevox::ConnectionHandler` concept

```cpp
template <typename F>
concept ConnectionHandler =
    requires(F f, std::uint64_t conn_id, aevox::TcpStream stream) {
        { f(conn_id, std::move(stream)) } -> std::same_as<Task<void>>;
    };
```

Constrains the handler lambda passed to `listen()`. The handler must accept `(uint64_t, TcpStream)` and return `Task<void>`. A static_assert fires at the call site if the constraint is not met.

---

### `aevox::Task<T>`

The coroutine return type for all async Aevox operations. See the dedicated [Task](task.md) reference page for the full API.

**Header:** `#include <aevox/task.hpp>` (included transitively by `executor.hpp`)

Quick reference:

```cpp
// void task — most connection handlers
aevox::Task<void> handle(std::uint64_t, aevox::TcpStream stream) {
    auto data = co_await stream.read();
    co_return;
}

// valued task — produces a result
aevox::Task<int> compute_answer() {
    co_return 42;
}

// chain with co_await
aevox::Task<void> outer(aevox::TcpStream stream) {
    int v = co_await compute_answer(); // v == 42
    co_return;
}
```

---

## Thread Safety

| Operation | Thread-safe? |
|---|---|
| `make_executor()` | Yes |
| `listen()` | No — call before `run()` only |
| `run()` | No — one thread only |
| `stop()` | **Yes** — call from any thread or signal handler |
| `thread_count()` | Yes |

---

## Performance Notes

- **TCP_NODELAY** is enabled on all accepted sockets. Nagle's algorithm is disabled by default — correct for request/response workloads where latency matters more than throughput of small writes.
- **Thread pool work stealing** balances load across cores automatically. CPU-bound handlers distribute without manual partitioning.
- **Coroutines are cheap** — each connection is one coroutine. Thousands can be in-flight simultaneously without OS thread overhead.
- **`conn_id` wraps after ~1.8×10¹⁹ connections** — at 10M connections/sec that is roughly 58,000 years.

---

## See Also

- [API Overview](index.md)
- [TcpStream](tcp_stream.md) — async TCP stream passed to every connection handler
- [Task](task.md) — `aevox::Task<T>` coroutine return type
- [Async Helpers](async.md) — `pool()`, `sleep()`, `when_all()`
- [Architecture Overview](../architecture/index.md)
- [Architecture — Executor](../architecture/executor.md) — design rationale for the Executor abstraction
- [Architecture — Coroutines](../architecture/coroutines.md) — how coroutines interact with the Executor
