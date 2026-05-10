#pragma once
// src/net/websocket_handshake.hpp
//
// INTERNAL — never included outside src/ or tests/.
//
// RFC 6455 upgrade validation and Sec-WebSocket-Accept computation.
// Pure functions — no shared state, no I/O, fully thread-safe.
//
// Design: Tasks/architecture/AEV-010-arch.md §4.2

#include <aevox/websocket_error.hpp>

#include <expected>
#include <string>
#include <string_view>

namespace aevox::net {

// =============================================================================
// HandshakeHeaders — minimal view of the headers needed for WebSocket upgrade.
// =============================================================================

/// Minimal header view passed into validate().
struct HandshakeHeaders
{
    std::string_view upgrade;           ///< Value of the "Upgrade" header (may be empty).
    std::string_view connection;        ///< Value of the "Connection" header (may be empty).
    std::string_view sec_websocket_key; ///< Value of the "Sec-WebSocket-Key" header (may be empty).
};

// =============================================================================
// validate — check that all three required upgrade headers are present.
// =============================================================================

/**
 * @brief Validates WebSocket upgrade headers per RFC 6455 §4.2.1.
 *
 * Checks:
 * - `Upgrade` header value equals "websocket" (case-insensitive).
 * - `Connection` header value contains "upgrade" (case-insensitive).
 * - `Sec-WebSocket-Key` header is non-empty.
 *
 * @param headers  The three header values extracted from the HTTP request.
 * @return         `std::monostate` on success.
 *                 `WebSocketError{invalid_handshake, ...}` if any check fails.
 */
[[nodiscard]] std::expected<void, aevox::WebSocketError> validate(
    const HandshakeHeaders& headers) noexcept;

// =============================================================================
// compute_accept_key — SHA-1 + Base64 per RFC 6455 §4.2.2.
// =============================================================================

/**
 * @brief Computes the `Sec-WebSocket-Accept` header value.
 *
 * Formula (RFC 6455 §4.2.2):
 * ```
 * base64(sha1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
 * ```
 *
 * @param client_key  The raw (undecoded) value of `Sec-WebSocket-Key`.
 * @return            Base64-encoded SHA-1 string, e.g. `"s3pPLMBiTxaQ9kYGzzhZRbK+xOo="`.
 */
[[nodiscard]] std::string compute_accept_key(std::string_view client_key) noexcept;

} // namespace aevox::net
