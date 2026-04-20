# Async Patterns

Aevox is built around C++20 coroutines. Every I/O operation suspends the current coroutine and resumes it when the result is ready — no threads are blocked waiting. This page shows the common async patterns you will use in handlers.

## How Aevox Async Works

Handlers that perform I/O must be coroutines — functions that return `aevox::Task<T>` and use `co_await` to suspend at I/O points. When a handler calls `co_await stream.read()`, it suspends immediately. The I/O thread that was running it is freed to handle other connections. When the OS delivers data, the framework resumes the coroutine automatically with the result.

There are no callbacks, no futures, and no manual state machines. The code reads like synchronous code while executing asynchronously. For the mechanics behind this, see [Coroutines and Task<T>](../architecture/coroutines.md).

## co_await in a Handler

The following handler reads data from the connection, echoes it back, and handles errors at each step:

```cpp
aevox::Task<void> echo_handler(std::uint64_t conn_id, aevox::TcpStream stream) {
    for (;;) {
        auto data = co_await stream.read();
        if (!data) {
            // IoError::Eof means the client closed the connection cleanly
            // Other IoError values indicate a network problem
            co_return;
        }

        auto write_result = co_await stream.write(*data);
        if (!write_result) {
            co_return;
        }
    }
}
```

Both `stream.read()` and `stream.write()` return `std::expected`. Always check the result before continuing. On an error, `co_return` exits the coroutine and the connection is closed automatically when `stream` is destroyed.

## CPU-Bound Work with pool()

If a handler needs to do heavy computation, use `aevox::pool()` to run it on the dedicated CPU thread pool. This keeps the I/O threads free to accept new connections.

```cpp
aevox::Task<void> compress_handler(std::uint64_t, aevox::TcpStream stream) {
    auto data = co_await stream.read();
    if (!data) co_return;

    // compress() runs on the CPU pool — the I/O thread is free during this
    auto compressed = co_await aevox::pool([bytes = *data]() {
        return compress(bytes);   // your CPU-intensive function
    });

    auto result = co_await stream.write(std::span{compressed});
    if (!result) co_return;
}
```

`aevox::pool()` takes a callable with no arguments and returns `aevox::Task<R>` where `R` is the return type of the callable. The I/O thread is released while the callable runs. When the CPU work is done, the coroutine resumes on an I/O thread.

## Timers with sleep()

`aevox::sleep()` suspends the coroutine for a duration without occupying any thread:

```cpp
#include <aevox/async.hpp>
#include <chrono>

using namespace std::chrono_literals;

aevox::Task<void> rate_limited_handler(std::uint64_t, aevox::TcpStream stream) {
    // Wait 100 ms before responding — no thread is occupied during the wait
    co_await aevox::sleep(100ms);

    auto result = co_await stream.write(/* response bytes */);
    if (!result) co_return;
}
```

The timer is implemented with an Asio timer under the hood. The OS wakes up the coroutine when the duration expires.

## Concurrent Fan-Out with when_all()

`aevox::when_all()` runs multiple tasks concurrently and waits for all of them to finish. Use it when you have independent I/O operations that can proceed in parallel:

```cpp
aevox::Task<void> parallel_handler(aevox::TcpStream stream_a, aevox::TcpStream stream_b) {
    // Both reads proceed concurrently — the coroutine resumes when both are done
    auto [data_a, data_b] = co_await aevox::when_all(
        stream_a.read(),
        stream_b.read()
    );

    if (!data_a || !data_b) co_return;

    // Process both results
}
```

`when_all()` accepts two or more `Task<T>` arguments (T must not be `void`) and returns a `Task<std::tuple<Ts...>>`. All tasks start immediately and the outer coroutine resumes only after every task completes.

## Graceful Shutdown

`aevox::App::stop()` is thread-safe and can be called from a signal handler. `app.listen()` installs SIGINT and SIGTERM handlers automatically, so Ctrl-C triggers a clean shutdown by default.

If you need to control shutdown programmatically:

```cpp
#include <aevox/app.hpp>
#include <csignal>

aevox::App* g_app = nullptr;

int main() {
    aevox::App app;
    g_app = &app;

    app.get("/", [](aevox::Request&) {
        return aevox::Response::ok("Hello");
    });

    std::signal(SIGINT, [](int) { g_app->stop(); });

    app.listen(8080);
    // Returns after drain completes
}
```

The shutdown sequence:

- All accept loops are closed — no new connections are accepted.
- In-flight coroutines are given the drain timeout (default 30 s) to finish their work.
- After the timeout, remaining coroutines have pending I/O cancelled and their frames are destroyed.

## Error Handling in Async Code

In a coroutine handler, the standard pattern is: check the result after every `co_await`, and `co_return` immediately if there is an error.

```cpp
aevox::Task<void> safe_handler(std::uint64_t, aevox::TcpStream stream) {
    auto data = co_await stream.read();
    if (!data) {
        // Connection error or EOF — log and exit silently
        co_return;
    }

    // Process *data and build a response
    auto response_bytes = process(*data);

    auto write_result = co_await stream.write(response_bytes);
    if (!write_result) co_return;
}
```

Do not `throw` from a handler. Exceptions that escape a coroutine are caught by the executor, logged (if logging is configured), and cause the connection to be closed. This is a last-resort safety net — rely on `std::expected` checks instead.

## See Also

- [Error Handling](error-handling.md) — the full `std::expected` error model and error type reference
- [API Reference — Async Helpers](../api/async.md) — complete reference for `pool()`, `sleep()`, `when_all()`
- [API Reference — Task](../api/task.md) — `aevox::Task<T>` coroutine return type
- [Architecture — Coroutines](../architecture/coroutines.md) — how coroutines and `Task<T>` work under the hood
