#pragma once
// include/aevox/websocket_error.hpp
//
// Structured error type for all WebSocket operations.
// Standard C++ only — no third-party types.
//
// Thread-safety: Value type — safe to copy and move across threads.
// Move semantics: Moved-from WebSocketError has message() returning empty view.

#include <cstdint>
#include <string>
#include <string_view>

namespace aevox {

// =============================================================================
// WebSocketErrorCode
// =============================================================================

/**
 * @brief Discriminates WebSocket error categories.
 *
 * Every `WebSocketError` carries one of these codes.
 * The codes cover both the handshake and the frame-I/O phases.
 */
enum class WebSocketErrorCode : std::uint8_t
{
    InvalidHandshake, ///< HTTP upgrade headers missing or malformed.
    ProtocolError,    ///< RFC 6455 protocol violation (bad opcode, bad mask bit, etc.).
    Closed,           ///< The WebSocket has already been closed; the operation is a no-op.
    SendFailed,       ///< A frame could not be sent (connection reset, buffer overflow).
    FrameTooLarge,    ///< Payload exceeds AppConfig::max_body_size.
};

// =============================================================================
// WebSocketError
// =============================================================================

/**
 * @brief Structured error type for WebSocket failures.
 *
 * Returned via `std::unexpected` from all WebSocket operations. Never thrown.
 *
 * @note Thread-safety: value type — safe to copy and move across threads.
 * @note Move semantics: moved-from `WebSocketError` has `message()` returning an
 *       empty `std::string_view`. Remains valid and destructible.
 * @note Ownership: stores the message string by value.
 */
class WebSocketError
{
public:
    /**
     * @brief Constructs a `WebSocketError` with a code and human-readable message.
     *
     * @param code     Category of the failure.
     * @param message  Description (e.g. "Missing Sec-WebSocket-Key header").
     *                 Stored by value — ownership is taken.
     */
    explicit WebSocketError(WebSocketErrorCode code, std::string message) noexcept;

    /**
     * @brief Returns the error category.
     *
     * @return `WebSocketErrorCode` discriminant.
     */
    [[nodiscard]] WebSocketErrorCode code() const noexcept;

    /**
     * @brief Returns the human-readable description.
     *
     * @return Non-owning view into the stored message string.
     *         Valid for the lifetime of this `WebSocketError`.
     *         Returns an empty view for a moved-from `WebSocketError`.
     */
    [[nodiscard]] std::string_view message() const noexcept;

private:
    WebSocketErrorCode code_;
    std::string        message_;
};

} // namespace aevox
