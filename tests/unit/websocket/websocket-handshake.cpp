// Unit tests: WebSocket handshake validation and accept-key computation.
// ADD ref: Tasks/architecture/AEV-010-arch.md §8 (Unit Tests)
#include <catch2/catch_test_macros.hpp>

#include "net/websocket_handshake.hpp"

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("WebSocket handshake - accept key computation matches RFC 6455 Appendix B", "[websocket]")
{
    // RFC 6455 Appendix B known-good test vector.
    // Input:  Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
    // Output: Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
    const std::string accept = aevox::net::compute_accept_key("dGhlIHNhbXBsZSBub25jZQ==");
    REQUIRE(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("WebSocket handshake - missing Sec-WebSocket-Key returns invalid_handshake",
          "[websocket]")
{
    const aevox::net::HandshakeHeaders headers{
        .upgrade           = "websocket",
        .connection        = "Upgrade",
        .sec_websocket_key = "", // empty key
    };
    auto result = aevox::net::validate(headers);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code() == aevox::WebSocketErrorCode::InvalidHandshake);
    REQUIRE_FALSE(result.error().message().empty());
}

TEST_CASE("WebSocket handshake - Upgrade header value not websocket returns invalid_handshake",
          "[websocket]")
{
    const aevox::net::HandshakeHeaders headers{
        .upgrade           = "http", // wrong value
        .connection        = "Upgrade",
        .sec_websocket_key = "dGhlIHNhbXBsZSBub25jZQ==",
    };
    auto result = aevox::net::validate(headers);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code() == aevox::WebSocketErrorCode::InvalidHandshake);
}

TEST_CASE("WebSocket handshake - missing Connection Upgrade returns invalid_handshake",
          "[websocket]")
{
    const aevox::net::HandshakeHeaders headers{
        .upgrade           = "websocket",
        .connection        = "keep-alive", // does not contain "upgrade"
        .sec_websocket_key = "dGhlIHNhbXBsZSBub25jZQ==",
    };
    auto result = aevox::net::validate(headers);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code() == aevox::WebSocketErrorCode::InvalidHandshake);
}
