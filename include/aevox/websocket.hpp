#pragma once
// include/aevox/websocket.hpp
//
// Public WebSocket connection handle exposed to application handlers.
// No backend networking types. Implementation lives in src/net/.
//
// Thread-safety: send()/close() safe from any coroutine; strand serializes I/O.
//   subscribe()/publish() safe from any thread via TopicBus shared_mutex.
//
// Move semantics: Move-only. Moved-from WebSocket is in closed state;
//   all operations return WebSocketError::closed.
//
// Ownership: Destroying without close() triggers a close on the next event-loop tick.
//
// Design: Tasks/architecture/AEV-010-arch.md §3.2

#include <aevox/task.hpp>
#include <aevox/websocket_error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>

// Forward declaration — allows friend class declaration below without including
// any backend networking header. aevox::net::WebSocketSession is defined in src/net/.
namespace aevox::net {
class WebSocketSession;
} // namespace aevox::net

namespace aevox {

// =============================================================================
// WebSocket
// =============================================================================

/**
 * @brief Async WebSocket connection handle for application code.
 *
 * `WebSocket` wraps a live RFC 6455 connection that has already completed
 * the HTTP/1.1 upgrade handshake. The application interacts exclusively
 * with this type — it never sees the underlying backend socket.
 *
 * Construct exclusively via `co_await req.upgrade_websocket()` or via
 * the `App::ws()` callback — never directly.
 *
 * @note Thread-safety: `send()` and `close()` are safe to call from any
 *       coroutine on any executor thread. Concurrent sends are serialized via
 *       an internal backend strand in `src/net/`. `subscribe()` and `publish()`
 *       are safe to call from any thread — the `TopicBus` uses a `shared_mutex`.
 * @note Move semantics: move-only. A moved-from `WebSocket` is in the
 *       `Closed` state — all operations return
 *       `std::unexpected(WebSocketError{WebSocketErrorCode::Closed, "moved-from"})`.
 * @note Ownership: destroying a `WebSocket` without calling `close()` first
 *       triggers an immediate close handshake on the next event-loop tick
 *       (the destructor posts a close to the internal strand).
 */
class WebSocket
{
public:
    WebSocket(WebSocket&&) noexcept;
    WebSocket& operator=(WebSocket&&) noexcept;
    WebSocket(const WebSocket&)            = delete;
    WebSocket& operator=(const WebSocket&) = delete;
    ~WebSocket();

    // -------------------------------------------------------------------------
    // Send operations
    // -------------------------------------------------------------------------

    /**
     * @brief Sends a UTF-8 text frame to the connected client.
     *
     * The payload is serialized as an RFC 6455 text frame (`FIN=1, opcode=0x1`)
     * with no server-side masking (servers must NOT mask per RFC 6455 §5.3).
     * If a previous send is still in flight the new frame is enqueued and sent
     * in order on the internal strand — no caller serialization required.
     *
     * @param message  UTF-8 payload. The view must remain valid until the
     *                 returned `Task` completes (the data is copied into the
     *                 internal send buffer during this coroutine).
     * @return         `Task<std::expected<void, WebSocketError>>`.
     *                 `WebSocketErrorCode::Closed` if the connection is already closed.
     *                 `WebSocketErrorCode::SendFailed` on underlying I/O error.
     * @note  Thread-safety: safe to call from any coroutine on any thread.
     * @note  Move semantics: on a moved-from `WebSocket` always returns
     *        `std::unexpected(WebSocketError{WebSocketErrorCode::Closed, "moved-from"})`.
     * @throws Nothing. All errors surface via `std::unexpected`.
     */
    [[nodiscard]] aevox::Task<std::expected<void, WebSocketError>> send(std::string_view message);

    /**
     * @brief Sends a binary frame to the connected client.
     *
     * Identical to the text overload except the opcode is `0x2` (binary).
     *
     * @param data  Binary payload bytes. The span must remain valid until the
     *              returned `Task` completes.
     * @return      `Task<std::expected<void, WebSocketError>>`. Same error
     *              conditions as `send(std::string_view)`.
     * @note  Thread-safety: same as `send(std::string_view)`.
     * @throws Nothing.
     */
    [[nodiscard]] aevox::Task<std::expected<void, WebSocketError>> send(
        std::span<const std::byte> data);

    /**
     * @brief Synchronous fire-and-forget send for use from non-coroutine callbacks.
     *
     * Builds the RFC 6455 text frame and enqueues it immediately without
     * returning a `Task`. Intended for use from synchronous `WebSocketHandler`
     * callbacks (e.g. `on_message`) where `co_await` is not available.
     *
     * If the session is already closed, the call is silently discarded.
     * If the send queue is full (> 128 pending frames), the frame is dropped
     * and the error is silently discarded — back-pressure is not available from
     * a synchronous context.
     *
     * **Prefer `co_await ws.send(msg)` from coroutine contexts** where the
     * result can be inspected. Use `send_nowait()` only when the caller cannot
     * `co_await` (e.g. inside a `std::function<void(...)>` callback).
     *
     * @param message  UTF-8 payload. Copied into the internal frame buffer —
     *                 the view does not need to remain valid after this call.
     * @note  Thread-safety: safe to call from any thread. Enqueue is protected
     *        by the internal strand post.
     * @throws Nothing.
     */
    void send_nowait(std::string_view message) noexcept;

    // -------------------------------------------------------------------------
    // Close
    // -------------------------------------------------------------------------

    /**
     * @brief Initiates a graceful WebSocket close handshake.
     *
     * Sends an RFC 6455 Close frame with `code` and `reason`, then waits for
     * the peer's echoing Close frame (or a timeout). After the close is
     * complete, the `on_close` callback registered in `WebSocketHandler` is
     * invoked with `code`.
     *
     * Calling `close()` on an already-closed `WebSocket` is a safe no-op that
     * returns `std::unexpected(WebSocketError{closed, ...})`.
     *
     * @param code    RFC 6455 close status code. 1000 = normal closure (default).
     *                Codes 0–999 and 1004, 1005, 1006, 1015 are reserved and
     *                must not be sent; passing them produces `protocol_error`.
     * @param reason  Optional UTF-8 reason string (RFC 6455 §5.5.1).
     *                Max 123 bytes — truncated silently if longer.
     * @return        `Task<std::expected<void, WebSocketError>>`.
     * @note  Thread-safety: safe to call from any coroutine on any thread.
     * @throws Nothing.
     */
    [[nodiscard]] aevox::Task<std::expected<void, WebSocketError>> close(
        std::uint16_t code = 1000, std::string_view reason = {});

    // -------------------------------------------------------------------------
    // Publish / Subscribe
    // -------------------------------------------------------------------------

    /**
     * @brief Subscribes this WebSocket to a named topic.
     *
     * After subscribing, any call to `publish(topic, msg)` from any WebSocket
     * in the same App instance will deliver `msg` to this connection's
     * `on_message` handler (unless `msg` was sent by this connection itself —
     * self-publish is suppressed).
     *
     * Subscribing to the same topic more than once is idempotent.
     *
     * @param topic  Arbitrary string topic name (e.g. `"room:42"`, `"chat"`).
     * @note  Thread-safety: safe to call from any thread. Uses the App-level
     *        `TopicBus` which is protected by a `std::shared_mutex`.
     */
    void subscribe(std::string_view topic);

    /**
     * @brief Publishes a text message to all WebSockets subscribed to `topic`.
     *
     * Delivery is best-effort and asynchronous: each subscriber's `on_message`
     * handler is scheduled on the subscriber's executor strand. Self-publish is
     * suppressed — the calling WebSocket does not receive its own message.
     *
     * Dead subscriber entries (closed connections) are lazily pruned on
     * each publish call.
     *
     * @param topic    The topic name to publish on.
     * @param message  UTF-8 message payload.
     * @note  Thread-safety: safe to call from any thread.
     */
    void publish(std::string_view topic, std::string_view message);

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the first topic this WebSocket subscribed to, if any.
     *
     * Convenience accessor: returns the topic registered via the most recent
     * `subscribe()` call. Returns an empty string_view if no topic is
     * subscribed. Useful in `on_message` handlers that broadcast back to the
     * sender's topic.
     *
     * @return Non-owning view into the stored topic string.
     *         Valid for the lifetime of this `WebSocket`.
     * @note   Not thread-safe — call only from the connection-owning coroutine.
     */
    [[nodiscard]] std::string_view topic() const noexcept;

    /**
     * @brief Returns the remote IP address of this WebSocket connection.
     *
     * Format: `"x.x.x.x"` for IPv4 (IPv6 is not supported in v0.2).
     * Returns `"unknown"` if the address is not available (e.g. moved-from).
     *
     * @return Non-owning view into the stored address string.
     *         Valid for the lifetime of this `WebSocket`.
     * @note   Not thread-safe — call only from the connection-owning coroutine.
     */
    [[nodiscard]] std::string_view remote_address() const noexcept;

private:
    /**
     * @brief Internal implementation type — holds the backend session.
     *
     * Defined only in `src/net/websocket_session.hpp`. Never visible to
     * application code. The full type is accessed only by `src/net/`.
     */
    struct Impl;
    std::unique_ptr<Impl> impl_;

    /**
     * @brief Private constructor — called only by `make_websocket_handle()`.
     *
     * Application code constructs `WebSocket` exclusively via
     * `co_await req.upgrade_websocket()` or `App::ws()`. Direct construction
     * is not possible.
     */
    explicit WebSocket(std::unique_ptr<Impl> impl) noexcept;

    // Internal factory function — declared without namespace qualifier,
    // resolved to aevox::make_websocket_handle by ADL once definition is visible.
    friend WebSocket make_websocket_handle(std::unique_ptr<Impl>) noexcept;

    // Internal Impl accessor — used by App::ws() dispatch in src/router/app_impl.cpp
    // to start the read loop and wait for session close. Resolved to
    // aevox::get_websocket_impl by ADL once the definition is visible (in websocket.cpp).
    friend Impl* get_websocket_impl(WebSocket&) noexcept;

    // WebSocketSession constructs WebSocket handles via make_handle().
    // It needs access to the private Impl type to populate it.
    friend class net::WebSocketSession;
};

} // namespace aevox
