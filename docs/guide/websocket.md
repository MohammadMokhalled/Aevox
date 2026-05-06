# WebSocket

Aevox provides first-class WebSocket support via `App::ws()`. The HTTP upgrade handshake is performed automatically — your callbacks receive a live `aevox::WebSocket` handle ready for bidirectional messaging. An in-process pub/sub bus is built-in so connections can broadcast to each other without any external message broker.

## Basic Echo Handler

Register a WebSocket route with `App::ws(pattern, handler)`. The `WebSocketHandler` aggregate accepts three optional callbacks: `on_open`, `on_message`, and `on_close`.

```cpp
#include <aevox/app.hpp>
#include <aevox/websocket_handler.hpp>

int main()
{
    aevox::App app;

    app.ws("/echo", aevox::WebSocketHandler{
        .on_open = [](aevox::WebSocket& ws) {
            // Connection established. Send a greeting.
            ws.send_nowait("Welcome!");
        },
        .on_message = [](aevox::WebSocket& ws, std::string_view msg) {
            // Echo back whatever the client sent.
            ws.send_nowait(msg);
        },
        .on_close = [](aevox::WebSocket& /*ws*/, std::uint16_t code) {
            // 1000 = normal closure.
            (void)code;
        },
    });

    app.listen(8080);
}
```

All three callbacks are optional. If `on_message` is omitted, incoming messages are silently discarded.

## Sending Messages

`send_nowait()` is the simplest way to send from inside a synchronous callback. For coroutine contexts, use `co_await ws.send(msg)` to inspect the result:

```cpp
app.ws("/echo", aevox::WebSocketHandler{
    .on_message = [](aevox::WebSocket& ws, std::string_view msg) {
        // Fire-and-forget — errors are silently discarded.
        ws.send_nowait(msg);
    },
});

// --- or, from a coroutine handler: ---
app.get("/send-ws", [](aevox::Request& /*req*/, aevox::Response& /*res*/) -> aevox::Task<void> {
    // (ws handle obtained elsewhere)
    // auto result = co_await ws.send("hello");
    // if (!result) { /* handle WebSocketErrorCode::Closed or SendFailed */ }
    co_return;
});
```

`co_await ws.send(msg)` returns `std::expected<void, aevox::WebSocketError>`. Check the result to detect closed or failed connections:

```cpp
auto result = co_await ws.send("hello");
if (!result) {
    const auto& err = result.error();
    if (err.code() == aevox::WebSocketErrorCode::Closed) {
        // Connection closed before the send completed.
    }
    // err.message() contains a human-readable description.
}
```

## Pub/Sub Room Broadcast

Use `ws.subscribe(topic)` and `ws.publish(topic, message)` to build in-process broadcast rooms. Self-publish is suppressed — a connection does not receive its own messages.

```cpp
app.ws("/room/{id}", aevox::WebSocketHandler{
    .on_open = [](aevox::WebSocket& ws) {
        // ws.topic() returns the first path parameter value — "room:42" etc.
        ws.subscribe(ws.topic());
    },
    .on_message = [](aevox::WebSocket& ws, std::string_view msg) {
        // Broadcast to everyone else in the same room.
        ws.publish(ws.topic(), msg);
    },
    .on_close = [](aevox::WebSocket& /*ws*/, std::uint16_t code) {
        // Subscription is cleaned up automatically when the session closes.
        (void)code;
    },
});
```

Any number of clients connecting to `/room/lobby` will receive each other's messages. Closed connections are pruned lazily on the next publish — no manual cleanup needed.

## Graceful Close

Initiate a close from application code with `co_await ws.close(code, reason)`:

```cpp
.on_message = [](aevox::WebSocket& ws, std::string_view msg) {
    if (msg == "quit") {
        // close() is a coroutine — use send_nowait for fire-and-forget in sync callbacks.
        ws.send_nowait("Goodbye!");
        // For a graceful close from a coroutine: co_await ws.close(1000, "Normal closure");
    }
},
```

Close codes follow RFC 6455. Common values: `1000` (normal), `1001` (going away), `1011` (server error).

`close()` on an already-closed connection is a safe no-op that returns `std::unexpected(WebSocketError{WebSocketErrorCode::Closed, ...})`.

## Error Handling

All fallible operations return `std::expected<void, aevox::WebSocketError>`. Never ignore the error branch:

```cpp
auto result = co_await ws.send("payload");
if (!result) {
    switch (result.error().code()) {
        case aevox::WebSocketErrorCode::Closed:
            // Peer closed the connection.
            break;
        case aevox::WebSocketErrorCode::SendFailed:
            // Underlying I/O error — connection is broken.
            break;
        default:
            break;
    }
}
```

`ws.send_nowait()` silently discards errors. Use it only when the send result does not matter (e.g. best-effort notifications).

## See Also

- [WebSocket API Reference](../api/websocket.md) — full class and method documentation
- [Async Patterns](async-patterns.md) — coroutine usage, `Task<T>`, and executor model
- [Request and Response](request-response.md) — HTTP upgrade via `req.upgrade_websocket()`
