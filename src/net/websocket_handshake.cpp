// src/net/websocket_handshake.cpp
//
// INTERNAL — RFC 6455 upgrade validation and accept-key computation.
// No Asio types. No exceptions. Pure functions.
//
// Design: Tasks/architecture/AEV-010-arch.md §4.2

#include "net/websocket_handshake.hpp"

#include <aevox/websocket_error.hpp>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "net/base64.hpp"
#include "net/sha1.hpp"

namespace aevox::net {

namespace {

/// RFC 6455 §4.2.2 GUID appended to Sec-WebSocket-Key before SHA-1.
constexpr std::string_view kWebSocketGUID{"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"};

/// Case-insensitive equality check for ASCII strings.
[[nodiscard]] bool iequal(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

/// Returns true if `haystack` case-insensitively contains `needle`.
[[nodiscard]] bool icontains(std::string_view haystack, std::string_view needle) noexcept
{
    if (needle.empty())
        return true;
    if (haystack.size() < needle.size())
        return false;

    // Linear scan comparing substrings case-insensitively.
    for (std::size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
        if (iequal(haystack.substr(i, needle.size()), needle))
            return true;
    }
    return false;
}

} // anonymous namespace

// =============================================================================
// validate
// =============================================================================

std::expected<void, aevox::WebSocketError> validate(const HandshakeHeaders& headers) noexcept
{
    // Check "Upgrade: websocket" (RFC 6455 §4.2.1 item 3).
    if (!iequal(headers.upgrade, "websocket")) {
        return std::unexpected(aevox::WebSocketError{aevox::WebSocketErrorCode::InvalidHandshake,
                                                     "Upgrade header must be 'websocket'"});
    }

    // Check "Connection" contains "upgrade" (RFC 6455 §4.2.1 item 4).
    if (!icontains(headers.connection, "upgrade")) {
        return std::unexpected(aevox::WebSocketError{aevox::WebSocketErrorCode::InvalidHandshake,
                                                     "Connection header must contain 'Upgrade'"});
    }

    // Check "Sec-WebSocket-Key" is non-empty (RFC 6455 §4.2.1 item 5).
    if (headers.sec_websocket_key.empty()) {
        return std::unexpected(aevox::WebSocketError{aevox::WebSocketErrorCode::InvalidHandshake,
                                                     "Missing Sec-WebSocket-Key header"});
    }

    return {};
}

// =============================================================================
// compute_accept_key
// =============================================================================

std::string compute_accept_key(std::string_view client_key) noexcept
{
    // Concatenate client_key + GUID.
    std::string combined;
    combined.reserve(client_key.size() + kWebSocketGUID.size());
    combined.append(client_key);
    combined.append(kWebSocketGUID);

    std::vector<std::uint8_t> sha_input;
    sha_input.reserve(combined.size());
    for (const char ch : combined) {
        sha_input.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(ch)));
    }

    const Sha1Digest digest = sha1_compute(sha_input);

    // Base64-encode the 20-byte digest.
    return base64_encode(std::span<const std::uint8_t>{digest.data(), digest.size()});
}

} // namespace aevox::net
