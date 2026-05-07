// Unit tests: RFC 6455 frame parser and emitter.
// ADD ref: Tasks/architecture/AEV-010-arch.md §8 (Unit Tests)
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "net/websocket_frame.hpp"

namespace {

std::vector<std::byte> str_to_bytes(std::string_view s)
{
    std::vector<std::byte> v;
    v.reserve(s.size());
    for (auto c : s) {
        v.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return v;
}

std::string bytes_to_str(const std::vector<std::byte>& v)
{
    std::string s;
    s.reserve(v.size());
    for (auto b : v) {
        s.push_back(static_cast<char>(static_cast<unsigned char>(b)));
    }
    return s;
}

} // namespace

// =============================================================================
// Parse unmasked server-to-client text frame
// =============================================================================

TEST_CASE("WebSocket frame - parse unmasked server-to-client text frame", "[websocket]")
{
    // Hand-crafted 7-byte frame: FIN=1, opcode=text(0x1), no mask, payload="hello" (5 bytes)
    // Byte 0: 0x81 (FIN=1, opcode=0x1)
    // Byte 1: 0x05 (MASK=0, length=5)
    // Bytes 2-6: 'h','e','l','l','o'
    const std::vector<std::byte> frame{std::byte{0x81}, std::byte{0x05}, std::byte{'h'},
                                       std::byte{'e'},  std::byte{'l'},  std::byte{'l'},
                                       std::byte{'o'}};

    // expect_masked = false: server frames are not masked.
    auto result = aevox::net::parse_frame(std::span<const std::byte>{frame},
                                          std::size_t{1024U} * 1024U, false);

    REQUIRE(result.has_value());
    REQUIRE(result->frame.opcode == aevox::net::Opcode::Text);
    REQUIRE(result->frame.fin == true);
    REQUIRE(result->bytes_consumed == 7);
    REQUIRE(bytes_to_str(result->frame.payload) == "hello");
}

// =============================================================================
// Parse masked client-to-server text frame
// =============================================================================

TEST_CASE("WebSocket frame - parse masked client-to-server text frame", "[websocket]")
{
    SECTION("Happy path - masked Hello frame with known masking key")
    {
        // Encode "Hello" with masking key {0x37, 0xFA, 0x21, 0x3D}.
        // Masked bytes = original XOR key[i % 4]:
        //   H(0x48) ^ 0x37 = 0x7F
        //   e(0x65) ^ 0xFA = 0x9F
        //   l(0x6C) ^ 0x21 = 0x4D
        //   l(0x6C) ^ 0x3D = 0x51
        //   o(0x6F) ^ 0x37 = 0x58
        const std::vector<std::byte> frame{std::byte{0x81}, // FIN=1, opcode=text
                                           std::byte{0x85}, // MASK=1, length=5
                                           std::byte{0x37}, std::byte{0xFA}, std::byte{0x21},
                                           std::byte{0x3D}, // mask key
                                           std::byte{0x7F}, std::byte{0x9F}, std::byte{0x4D},
                                           std::byte{0x51}, std::byte{0x58}};

        auto result = aevox::net::parse_frame(std::span<const std::byte>{frame},
                                              std::size_t{1024U} * 1024U, true);

        REQUIRE(result.has_value());
        REQUIRE(result->frame.opcode == aevox::net::Opcode::Text);
        REQUIRE(result->frame.fin == true);
        REQUIRE(bytes_to_str(result->frame.payload) == "Hello");
    }

    SECTION("Error - frame with MASK bit clear from client direction (protocol_error)")
    {
        // Unmasked frame (server-style) presented as client frame.
        const std::vector<std::byte> frame{std::byte{0x81}, std::byte{0x05}, std::byte{'h'},
                                           std::byte{'e'},  std::byte{'l'},  std::byte{'l'},
                                           std::byte{'o'}};

        auto result = aevox::net::parse_frame(std::span<const std::byte>{frame},
                                              std::size_t{1024U} * 1024U, true /* expect_masked */);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code() == aevox::WebSocketErrorCode::ProtocolError);
    }

    SECTION("Error - reserved opcode 0x3 (protocol_error)")
    {
        // Opcode 0x3 is reserved — should be rejected.
        // Build a masked frame with opcode 0x3.
        const std::vector<std::byte> frame{
            std::byte{0x83}, // FIN=1, opcode=0x3 (reserved)
            std::byte{0x81}, // MASK=1, length=1
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, // mask key
            std::byte{0x41} // 'A' masked with 0x00 = 'A'
        };

        auto result = aevox::net::parse_frame(std::span<const std::byte>{frame},
                                              std::size_t{1024U} * 1024U, true);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code() == aevox::WebSocketErrorCode::ProtocolError);
    }

    SECTION("Edge - zero-length masked payload is valid")
    {
        // Zero-length masked frame: FIN=1, opcode=text, MASK=1, length=0.
        const std::vector<std::byte> frame{
            std::byte{0x81},                                                   // FIN=1, opcode=text
            std::byte{0x80},                                                   // MASK=1, length=0
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00} // mask key
        };

        auto result = aevox::net::parse_frame(std::span<const std::byte>{frame},
                                              std::size_t{1024U} * 1024U, true);

        REQUIRE(result.has_value());
        REQUIRE(result->frame.payload.empty());
        REQUIRE(result->bytes_consumed == 6); // 2 header + 4 mask
    }

    SECTION("Edge - 16-bit extended payload length (126 bytes of zeros)")
    {
        // Frame header: FIN=1, opcode=binary, MASK=1, length=126 (16-bit extended).
        // Byte 0: 0x82 (FIN=1, opcode=binary=0x2)
        // Byte 1: 0xFE (MASK=1, len7=126 → 16-bit extended)
        // Bytes 2-3: 0x00 0x7E = 126 (payload length)
        // Bytes 4-7: masking key = {0x00, 0x00, 0x00, 0x00}
        // Bytes 8-133: 126 zero bytes (masked with 0x00 → still zeros)
        //
        // Total bytes consumed: 2 + 2 + 4 + 126 = 134

        std::vector<std::byte> frame;
        frame.push_back(std::byte{0x82}); // FIN=1, binary
        frame.push_back(std::byte{0xFE}); // MASK=1, len7=126
        frame.push_back(std::byte{0x00}); // extended length high byte
        frame.push_back(std::byte{0x7E}); // extended length low byte (=126)
        frame.push_back(std::byte{0x00}); // mask byte 0
        frame.push_back(std::byte{0x00}); // mask byte 1
        frame.push_back(std::byte{0x00}); // mask byte 2
        frame.push_back(std::byte{0x00}); // mask byte 3
        for (int i = 0; i < 126; ++i) {
            frame.push_back(std::byte{0x00}); // payload (zeros masked with zeros = zeros)
        }

        auto result = aevox::net::parse_frame(std::span<const std::byte>{frame},
                                              std::size_t{1024U} * 1024U, true);

        REQUIRE(result.has_value());
        REQUIRE(result->bytes_consumed == 134);
        REQUIRE(result->frame.payload.size() == 126);
    }

    SECTION("Edge - incomplete masked frame is reported distinctly")
    {
        const std::vector<std::byte> frame{std::byte{0x81}, std::byte{0x85}, std::byte{0x00},
                                           std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                                           std::byte{'h'},  std::byte{'e'}};

        auto result = aevox::net::parse_frame_detailed(std::span<const std::byte>{frame},
                                                       std::size_t{1024U} * 1024U, true);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().kind == aevox::net::ParseFrameErrorKind::Incomplete);
        REQUIRE(result.error().error.code() == aevox::WebSocketErrorCode::ProtocolError);
    }
}

// =============================================================================
// Emit server-to-client text frame
// =============================================================================

TEST_CASE("WebSocket frame - emit server-to-client text frame", "[websocket]")
{
    // Emit "hello" (5 bytes) as a text frame.
    // Expected: {0x81, 0x05, 'h', 'e', 'l', 'l', 'o'}
    const auto payload = str_to_bytes("hello");
    const auto frame =
        aevox::net::emit_frame(aevox::net::Opcode::Text, true, std::span<const std::byte>{payload});

    REQUIRE(frame.size() == 7);
    REQUIRE(static_cast<uint8_t>(frame[0]) == 0x81); // FIN=1, opcode=text
    REQUIRE(static_cast<uint8_t>(frame[1]) == 0x05); // MASK=0, length=5
    REQUIRE(static_cast<uint8_t>(frame[2]) == 'h');
    REQUIRE(static_cast<uint8_t>(frame[3]) == 'e');
    REQUIRE(static_cast<uint8_t>(frame[4]) == 'l');
    REQUIRE(static_cast<uint8_t>(frame[5]) == 'l');
    REQUIRE(static_cast<uint8_t>(frame[6]) == 'o');
}
