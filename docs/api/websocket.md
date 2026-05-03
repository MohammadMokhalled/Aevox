# WebSocket API

> RFC 6455 WebSocket support: upgrade, send, receive, close, and in-process pub/sub broadcast.

## Overview

Aevox provides first-class WebSocket support via three public types:

- `aevox::WebSocket` — an async connection handle returned after a successful HTTP upgrade.
- `aevox::WebSocketHandler` — an aggregate of three lifecycle callbacks (`on_open`, `on_message`, `on_close`).
- `aevox::WebSocketError` / `aevox::WebSocketErrorCode` — structured error type for all WebSocket operations.

Register a WebSocket route with `App::ws(path, handler)`. Aevox performs the RFC 6455 handshake automatically; your callbacks are invoked for each lifecycle event. No manual header inspection or 101 response construction is needed.

In-process pub/sub is built-in: `WebSocket::subscribe(topic)` and `WebSocket::publish(topic, message)` let connections broadcast to each other without any external message broker.

## Quick Start

```cpp
#include <aevox/app.hpp>
#include <aevox/websocket_handler.hpp>

int main()
{
    aevox::App app;

    app.ws("/chat/{room}", aevox::WebSocketHandler{
        .on_open = [](aevox::WebSocket& ws) {
            // Subscribe to the room named in the path parameter.
            ws.subscribe(ws.topic());
        },
        .on_message = [](aevox::WebSocket& ws, std::string_view msg) {
            // Broadcast to all subscribers of this room.
            ws.publish(ws.topic(), msg);
        },
        .on_close = [](aevox::WebSocket& /*ws*/, std::uint16_t code) {
            // code == 1000 for normal closure.
            (void)code;
        },
    });

    app.listen(8080);
}
```

## API Reference

### `aevox::WebSocket`

Async WebSocket connection handle. Obtained exclusively via `App::ws()` — never constructed directly.

**Thread-safety:** `send()` and `close()` are safe to call from any coroutine on any thread. Concurrent sends are serialized via an internal Asio strand. `subscribe()` and `publish()` are safe from any thread.

**Move semantics:** Move-only. A moved-from `WebSocket` is in the closed state — all operations return `WebSocketError{Closed, ...}`.

**Ownership:** Destroying a `WebSocket` without calling `close()` triggers an immediate close on the next event-loop tick.

#### `send(string_view) -> Task<expected<void, WebSocketError>>`

```cpp
[[nodiscard]] aevox::Task<std::expected<void, WebSocketError>>
send(std::string_view message);
```

Sends a UTF-8 text frame. The view must remain valid until the returned `Task` completes. Use from coroutine contexts where the result can be inspected.

**Errors:** `WebSocketErrorCode::Closed` if the connection is already closed. `WebSocketErrorCode::SendFailed` on I/O error.

**Example:**

```cpp
app.get("/trigger", [](aevox::Request& /*req*/) -> aevox::Task<aevox::Response> {
    // ws is captured from outer scope in a real application
    // auto result = co_await ws.send("event fired");
    // if (!result) { /* handle result.error() */ }
    co_return aevox::Response::ok("triggered");
});
```

#### `send(span<const byte>) -> Task<expected<void, WebSocketError>>`

```cpp
[[nodiscard]] aevox::Task<std::expected<void, WebSocketError>>
send(std::span<const std::byte> data);
```

Sends a binary frame. Otherwise identical to the text overload.

#### `send_nowait(string_view) -> void`

```cpp
void send_nowait(std::string_view message) noexcept;
```

Synchronous fire-and-forget send. Use this from synchronous `WebSocketHandler` callbacks (`on_message`, `on_close`) where `co_await` is not available.

Errors are silently discarded. If the send queue exceeds 128 pending frames, the frame is dropped. Prefer `co_await ws.send(msg)` in coroutine contexts.

**Example:**

```cpp
aevox::WebSocketHandler{
    .on_message = [](aevox::WebSocket& ws, std::string_view msg) {
        ws.send_nowait(msg); // echo back — fire and forget
    },
}
```

#### `close(code, reason) -> Task<expected<void, WebSocketError>>`

```cpp
[[nodiscard]] aevox::Task<std::expected<void, WebSocketError>>
close(std::uint16_t code = 1000, std::string_view reason = {});
```

Initiates a graceful RFC 6455 close handshake. Sends a Close frame with `code` and `reason`, then waits for the peer's echoing Close frame (or a timeout). After completion, `on_close` is invoked.

Calling on an already-closed `WebSocket` returns `WebSocketErrorCode::Closed` (safe no-op).

**Error:** `WebSocketErrorCode::Closed` if already closed.

#### `subscribe(topic) -> void`

```cpp
void subscribe(std::string_view topic);
```

Subscribes this connection to a named topic. After subscribing, any `publish(topic, msg)` from any connection in the same `App` instance delivers `msg` to this connection's `on_message` callback (self-publish is suppressed). Subscribing to the same topic more than once is idempotent.

#### `publish(topic, message) -> void`

```cpp
void publish(std::string_view topic, std::string_view message);
```

Publishes a message to all connections subscribed to `topic`, except this connection itself. Delivery is asynchronous and best-effort. Dead subscriber entries are lazily pruned.

#### `topic() -> string_view`

```cpp
[[nodiscard]] std::string_view topic() const noexcept;
```

Returns the topic name set by the most recent `subscribe()` call, or an empty string if not subscribed. Useful inside `on_message` to broadcast back to the sender's room.

#### `remote_address() -> string_view`

```cpp
[[nodiscard]] std::string_view remote_address() const noexcept;
```

Returns the remote IPv4 address string (e.g. `"127.0.0.1"`). Returns `"unknown"` for moved-from connections.

---

### `aevox::WebSocketHandler`

Aggregate of three lifecycle callbacks. All fields default to no-op lambdas.

```cpp
struct WebSocketHandler
{
    std::function<void(WebSocket&)>                      on_open    = [](WebSocket&) {};
    std::function<void(WebSocket&, std::string_view)>    on_message = [](WebSocket&, std::string_view) {};
    std::function<void(WebSocket&, std::uint16_t)>       on_close   = [](WebSocket&, std::uint16_t) {};
};
```

| Callback | When invoked | Notes |
|---|---|---|
| `on_open` | After RFC 6455 handshake completes | Call `ws.subscribe(topic)` here |
| `on_message` | Each complete text or binary frame | View `msg` is valid only during the call |
| `on_close` | After close handshake completes | `code` is 1006 for abnormal closure |

All callbacks are synchronous. Use `ws.send_nowait(msg)` to send from `on_message`. Use `co_await ws.send(msg)` from a coroutine context for error feedback.

---

### `aevox::WebSocketError`

Structured error type for all WebSocket failures. Returned via `std::unexpected`.

```cpp
class WebSocketError
{
public:
    explicit WebSocketError(WebSocketErrorCode code, std::string message) noexcept;
    [[nodiscard]] WebSocketErrorCode code()    const noexcept;
    [[nodiscard]] std::string_view   message() const noexcept;
};
```

---

### `aevox::WebSocketErrorCode`

```cpp
enum class WebSocketErrorCode : std::uint8_t
{
    InvalidHandshake, ///< HTTP upgrade headers missing or malformed.
    ProtocolError,    ///< RFC 6455 protocol violation.
    Closed,           ///< Connection is already closed.
    SendFailed,       ///< Frame could not be sent (I/O error or queue overflow).
    FrameTooLarge,    ///< Payload exceeds AppConfig::max_body_size.
};
```

---

### `App::ws(path_pattern, handler)`

```cpp
void ws(std::string_view path_pattern, WebSocketHandler handler);
```

Registers a WebSocket route. The `path_pattern` follows the same typed-segment syntax as HTTP routes. If the request is not a valid WebSocket upgrade, Aevox responds with HTTP 400 automatically — `WebSocketHandler` callbacks are never invoked.

**Example:**

```cpp
app.ws("/ws",              handler);       // static path
app.ws("/chat/{room}",     handler);       // named segment
app.ws("/stream/{id}",     handler);       // named segment
```

---

### `Request::is_websocket_upgrade() -> bool`

```cpp
[[nodiscard]] bool is_websocket_upgrade() const noexcept;
```

Returns `true` if the request carries all three required WebSocket upgrade headers (`Upgrade: websocket`, `Connection: Upgrade`, `Sec-WebSocket-Key`). Cheap synchronous check — no I/O.

---

### `Request::upgrade_websocket() -> Task<expected<WebSocket, WebSocketError>>`

```cpp
[[nodiscard]] aevox::Task<std::expected<aevox::WebSocket, aevox::WebSocketError>>
upgrade_websocket();
```

Performs the HTTP/1.1 to WebSocket upgrade. Validates headers, writes the HTTP 101 response, and transfers TCP ownership to a `WebSocketSession`. On success, the `Request`'s underlying `TcpStream` is consumed — do not call any further HTTP methods on the request.

**Errors:** `WebSocketErrorCode::InvalidHandshake` if headers are absent or malformed. `WebSocketErrorCode::SendFailed` if writing the 101 response failed.

**Note:** You do not typically call this directly. `App::ws()` handles it automatically. Only use this when registering a custom GET handler that needs manual upgrade control.

---

### `Response::switching_protocols() -> Response`

```cpp
[[nodiscard]] static Response switching_protocols();
```

Internal factory that returns a 101 sentinel response. The connection handler detects this status code and suppresses writing it to the socket — the real HTTP 101 was already sent by `upgrade_websocket()`. Application code should not call this directly.

## Error Reference

| Error | Meaning | How to handle |
|---|---|---|
| `InvalidHandshake` | Missing or malformed upgrade headers | Return HTTP 400 (done automatically by `App::ws()`) |
| `ProtocolError` | RFC 6455 violation (bad opcode, unmasked client frame) | Session closes with code 1002; `on_close` fires |
| `Closed` | Operation on an already-closed connection | No-op; check `result.has_value()` in coroutine contexts |
| `SendFailed` | Frame could not be written to the socket | Treat as connection loss; do not retry |
| `FrameTooLarge` | Payload exceeded `AppConfig::max_body_size` | Session closes with code 1009 automatically |

## Thread Safety

- `WebSocket::send()`, `close()` — safe from any coroutine thread; Asio strand serializes.
- `WebSocket::subscribe()`, `publish()` — safe from any thread; `TopicBus` uses `std::shared_mutex`.
- `WebSocket::topic()`, `remote_address()` — safe only from the connection-owning coroutine.
- `App::ws()` — not thread-safe; call before `listen()`.

## Performance Notes

- Frame parsing: O(n) in payload size; no heap allocation for frames under 64 bytes.
- Topic bus fan-out: O(n) in subscriber count; dead entries pruned lazily on each publish.
- Send queue depth limit: 128 pending frames. Frames dropped silently when full (back-pressure).
- Close handshake timeout: not configurable in v0.2; defaults to next read timeout.

## See Also

- [WebSocket User Guide](../guide/websocket.md) — practical walkthrough with examples
- [Router and App API](router.md) — route registration
- [Request and Response API](request-response.md) — HTTP upgrade prerequisites
