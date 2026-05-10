// src/net/websocket.cpp
//
// INTERNAL — Out-of-line implementations for aevox::WebSocket PIMPL methods.
// WebSocket::Impl is defined in websocket_session.hpp (which this file includes).
// All Asio interaction goes through WebSocketSession.
//
// Design: Tasks/architecture/AEV-010-arch.md §3.2, §4.4

#include <aevox/websocket.hpp>

#include <utility>

#include "net/websocket_session.hpp" // provides complete WebSocket::Impl

namespace aevox {

// =============================================================================
// WebSocket — special members (out-of-line because Impl is incomplete in websocket.hpp)
// =============================================================================

WebSocket::WebSocket(std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)} {}

WebSocket::WebSocket(WebSocket&&) noexcept            = default;
WebSocket& WebSocket::operator=(WebSocket&&) noexcept = default;
WebSocket::~WebSocket()                               = default;

// =============================================================================
// WebSocket — send (text)
//
// Wraps the eager send_text() in a coroutine so that callers using co_await
// receive the result. For synchronous (non-coroutine) contexts, use
// send_nowait() instead.
// =============================================================================

aevox::Task<std::expected<void, WebSocketError>> WebSocket::send(std::string_view message)
{
    if (!impl_ || !impl_->session) {
        co_return std::unexpected(WebSocketError{WebSocketErrorCode::Closed, "moved-from"});
    }
    co_return impl_->session->send_text(message);
}

// =============================================================================
// WebSocket — send (binary)
// =============================================================================

aevox::Task<std::expected<void, WebSocketError>> WebSocket::send(std::span<const std::byte> data)
{
    if (!impl_ || !impl_->session) {
        co_return std::unexpected(WebSocketError{WebSocketErrorCode::Closed, "moved-from"});
    }
    co_return impl_->session->send_binary(data);
}

// =============================================================================
// WebSocket — send_nowait (synchronous fire-and-forget)
//
// Calls send_text() eagerly from any context (including non-coroutine sync
// callbacks). Does NOT return a Task — errors are silently discarded.
// Use only from WebSocketHandler callbacks or other sync contexts.
// =============================================================================

void WebSocket::send_nowait(std::string_view message) noexcept
{
    if (!impl_ || !impl_->session)
        return;
    // send_text() posts to the strand eagerly. On error (session closed or queue
    // full) the frame is silently dropped — callers needing feedback use send().
    if (!impl_->session->send_text(message).has_value()) {
        return;
    }
}

// =============================================================================
// WebSocket — close
// =============================================================================

aevox::Task<std::expected<void, WebSocketError>> WebSocket::close(std::uint16_t    code,
                                                                  std::string_view reason)
{
    if (!impl_ || !impl_->session) {
        co_return std::unexpected(WebSocketError{WebSocketErrorCode::Closed, "moved-from"});
    }
    co_return co_await impl_->session->close_graceful(code, reason);
}

// =============================================================================
// WebSocket — subscribe / publish
// =============================================================================

void WebSocket::subscribe(std::string_view topic)
{
    if (impl_ && impl_->session) {
        impl_->session->subscribe(topic);
    }
}

void WebSocket::publish(std::string_view topic, std::string_view message)
{
    if (impl_ && impl_->session) {
        impl_->session->publish(topic, message);
    }
}

// =============================================================================
// WebSocket — accessors
// =============================================================================

std::string_view WebSocket::topic() const noexcept
{
    if (!impl_ || !impl_->session)
        return {};
    return impl_->session->current_topic();
}

std::string_view WebSocket::remote_address() const noexcept
{
    if (!impl_ || !impl_->session)
        return "unknown";
    return impl_->session->remote_address();
}

// =============================================================================
// make_websocket_handle — internal factory (friend of WebSocket)
// =============================================================================

WebSocket make_websocket_handle(std::unique_ptr<WebSocket::Impl> impl) noexcept
{
    return WebSocket{std::move(impl)};
}

// =============================================================================
// get_websocket_impl — internal Impl accessor (friend of WebSocket)
//
// Used by App::ws() dispatch in app_impl.cpp to start the read loop and park
// the connection coroutine via session->wait_for_close().
// =============================================================================

WebSocket::Impl* get_websocket_impl(WebSocket& ws) noexcept
{
    return ws.impl_.get();
}

// =============================================================================
// WebSocketError — out-of-line implementations
// =============================================================================

WebSocketError::WebSocketError(WebSocketErrorCode code, std::string message) noexcept
    : code_{code}, message_{std::move(message)}
{}

WebSocketErrorCode WebSocketError::code() const noexcept
{
    return code_;
}

std::string_view WebSocketError::message() const noexcept
{
    return message_;
}

} // namespace aevox
