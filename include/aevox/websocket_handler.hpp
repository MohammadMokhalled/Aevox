#pragma once
// include/aevox/websocket_handler.hpp
//
// WebSocketHandler aggregate — callback container for WebSocket lifecycle events.
// No Asio types. Standard C++ only.
//
// Thread-safety: read-only after construction; concurrent reads from worker
//   threads are safe.
// Move semantics: move-constructible (default). Moved-from state: all callbacks
//   replaced by no-op lambdas.
// Ownership: App owns the WebSocketHandler until the App is destroyed.
//
// Design: Tasks/architecture/AEV-010-arch.md §3.3

#include <aevox/websocket.hpp>

#include <cstdint>
#include <functional>
#include <string_view>

namespace aevox {

// =============================================================================
// WebSocketHandler
// =============================================================================

/**
 * @brief Aggregate of callbacks for WebSocket lifecycle events.
 *
 * Pass to `App::ws()`. All three fields default to no-op lambdas —
 * only the events you care about need to be set.
 *
 * All callbacks are invoked on the executor strand that owns the connection
 * (ADR-3: coroutines pinned to the originating thread). Callbacks must not
 * block. For CPU-heavy processing offload via `co_await aevox::pool(fn)`.
 *
 * @note Thread-safety: the `WebSocketHandler` aggregate is read-only after
 *       construction and stored in `App::Impl`. Concurrent reads from worker
 *       threads are safe.
 * @note Move semantics: move-constructible (default). Moved-from state:
 *       all callbacks are replaced by no-op lambdas.
 * @note Ownership: `App` owns the `WebSocketHandler` until the `App` is
 *       destroyed.
 */
struct WebSocketHandler
{
    /**
     * @brief Called once after the upgrade handshake completes.
     *
     * The `WebSocket&` passed here is the live connection. The application may
     * call `ws.subscribe()` here to register the connection with a topic.
     *
     * @param ws  Reference to the newly opened WebSocket.
     *
     * @note Default: no-op lambda `[](WebSocket&) {}`.
     */
    std::function<void(WebSocket&)> on_open = [](WebSocket&) {};

    /**
     * @brief Called when a complete text or binary frame is received.
     *
     * The `msg` view is valid only for the duration of this call — copy it
     * if the value is needed beyond the callback return.
     *
     * Fragmented messages (FIN=0) are rejected at the protocol level in v0.2;
     * this callback is only invoked for complete single-frame messages (FIN=1).
     *
     * @param ws   Reference to the owning WebSocket.
     * @param msg  UTF-8 payload for text frames; raw bytes for binary frames.
     *             The view points into the session's internal read buffer.
     *
     * @note Default: no-op lambda `[](WebSocket&, std::string_view) {}`.
     */
    std::function<void(WebSocket&, std::string_view)> on_message = [](WebSocket&,
                                                                      std::string_view) {};

    /**
     * @brief Called after the connection close handshake completes.
     *
     * `code` is the RFC 6455 status code from the received Close frame
     * (1000 for normal closure, etc.). If the connection was closed due to
     * an I/O error before a Close frame was received, `code` is 1006
     * (Abnormal Closure — not sent on the wire, but used to signal to the
     * application that the close was not clean).
     *
     * The `WebSocket&` is in the closed state when this callback fires.
     * Calling any I/O method on it returns `WebSocketError::closed`.
     *
     * @param ws    Reference to the now-closed WebSocket.
     * @param code  RFC 6455 close status code.
     *
     * @note Default: no-op lambda `[](WebSocket&, std::uint16_t) {}`.
     */
    std::function<void(WebSocket&, std::uint16_t)> on_close = [](WebSocket&, std::uint16_t) {};
};

} // namespace aevox
