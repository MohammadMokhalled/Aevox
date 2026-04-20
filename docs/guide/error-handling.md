# Error Handling

Aevox uses `std::expected<T, E>` as its only error mechanism. No Aevox function throws an exception for a recoverable error. This page explains the model, lists all error types, and shows the propagation patterns you will use in handlers.

## The Error Model

`std::expected<T, E>` is a type that holds either a success value of type `T` or an error value of type `E`. It is the return type of every fallible Aevox operation, and it is marked `[[nodiscard]]` — the compiler warns if you discard the result without checking it.

```cpp
// The standard pattern: check before using
auto data = co_await stream.read();
if (!data) {
    // data.error() is the IoError value
    co_return;
}
// *data is the success value — a std::vector<std::byte>
```

No Aevox function throws for a recoverable error. If something goes wrong, the error appears in the `std::expected`. The only exceptions you might encounter are from user-supplied lambdas passed to `aevox::pool()`, or from `std::bad_alloc` — not from Aevox itself.

## Error Types

| Error type | Header | When it occurs |
|---|---|---|
| `aevox::ExecutorError` | `<aevox/executor.hpp>` | During `executor->listen()` or `executor->run()` |
| `aevox::IoError` | `<aevox/tcp_stream.hpp>` | During `stream.read()` or `stream.write()` |
| `aevox::RouteError` | `<aevox/router.hpp>` | During router dispatch (accessible in tests) |
| `aevox::ParamError` | `<aevox/request.hpp>` | During `req.param<T>()` |

### ExecutorError values

| Value | When it occurs |
|---|---|
| `bind_failed` | Port in use or insufficient permissions |
| `listen_failed` | `listen()` syscall failed after a successful bind |
| `accept_failed` | A single `accept()` call failed (non-fatal — loop continues) |
| `already_running` | `run()` called while the executor is already running |
| `not_running` | Operation on a stopped executor |

### IoError values

| Value | When it occurs |
|---|---|
| `Eof` | The client closed the connection cleanly |
| `Cancelled` | The I/O operation was cancelled (e.g. during shutdown) |
| `Reset` | The connection was reset by the peer |
| `Timeout` | The read or write timed out |
| `Unknown` | Any other OS-level I/O error |

### RouteError values

| Value | When it occurs |
|---|---|
| `NotFound` | No route pattern matched the request path — produces a 404 |
| `MethodNotAllowed` | Path matched but no handler for the HTTP method — produces a 405 |
| `BadParam` | Typed parameter conversion failed — produces a 400 |

### ParamError values

| Value | When it occurs |
|---|---|
| `NotFound` | No path parameter with the given name was captured |
| `BadConversion` | The raw string cannot be converted to the requested type |

## Propagating Errors in Coroutines

In coroutine handlers, use early `co_return` to exit on errors. The pattern is:

- For connection errors (`IoError`): `co_return` silently — the client has gone away.
- For application errors (`ParamError`, missing data): return an error response to the client.

```cpp
aevox::Task<void> get_user(std::uint64_t, aevox::TcpStream stream) {
    // Read the raw bytes from the connection
    auto data = co_await stream.read();
    if (!data) {
        // Connection error — exit without responding
        co_return;
    }

    // (In a real handler, the framework builds the Request for you)
    // For demonstration, check a parameter manually:
    aevox::Request req = /* built by framework */;
    auto id = req.param<int>("id");
    if (!id) {
        // Application error — tell the client
        auto body = std::string{"invalid id"};
        co_await stream.write(/* serialize bad_request response */);
        co_return;
    }

    // Success path: use *id
}
```

In practice, most handlers look like this — the framework handles request parsing and response serialization:

```cpp
app.get("/users/{id:int}", [](aevox::Request& req) {
    auto id = req.param<int>("id");
    if (!id) {
        return aevox::Response::bad_request("invalid user id");
    }
    return aevox::Response::ok(std::format("User {}", *id));
});
```

## to_string() for Diagnostics

All Aevox error enums have a `to_string()` overload that returns a human-readable `std::string_view`. Use it for logging and error messages:

```cpp
auto result = executor->listen(8080, handler);
if (!result) {
    std::println(stderr, "listen failed: {}", aevox::to_string(result.error()));
    return 1;
}
```

```cpp
auto data = co_await stream.read();
if (!data) {
    std::println("connection closed: {}", aevox::to_string(data.error()));
    co_return;
}
```

## What Happens on Unhandled Exceptions

If a handler function `throw`s and the exception escapes the coroutine boundary, the executor catches it. If a logger is configured, the exception message is logged. The connection is then closed. The server does not crash — other connections continue running.

Do not rely on this safety net. Always handle errors explicitly with `std::expected`. The exception fallback exists only as a last resort for unexpected `std::bad_alloc` or bugs in user code.

## See Also

- [Async Patterns](async-patterns.md) — `co_await` patterns and how to structure coroutine handlers
- [API Reference — Executor](../api/executor.md) — `ExecutorError` reference and `to_string()`
- [Architecture — Error Model](../architecture/error-model.md) — the design rationale behind `std::expected`
