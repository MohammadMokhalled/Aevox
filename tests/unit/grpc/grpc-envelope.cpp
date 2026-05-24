// gRPC message envelope encode/decode tests.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "plugins/grpc/grpc_envelope.hpp"

TEST_CASE("grpc envelope - encodes empty uncompressed message", "[grpc]")
{
    auto encoded = aevox::grpc::detail::encode_unary_message({}, 1024);
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->size() == 5U);
    CHECK((*encoded)[0] == std::byte{0});
}

TEST_CASE("grpc envelope - encodes non-empty uncompressed message", "[grpc]")
{
    std::array payload{std::byte{0xCA}, std::byte{0xFE}};
    auto       encoded = aevox::grpc::detail::encode_unary_message(payload, 1024);
    REQUIRE(encoded.has_value());
    REQUIRE(encoded->size() == 7U);
    CHECK((*encoded)[4] == std::byte{2});
    CHECK((*encoded)[5] == std::byte{0xCA});
    CHECK((*encoded)[6] == std::byte{0xFE});
}

TEST_CASE("grpc envelope - decodes empty uncompressed message", "[grpc]")
{
    std::array encoded{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    auto       decoded = aevox::grpc::detail::decode_unary_message(encoded, 1024);
    REQUIRE(decoded.has_value());
    CHECK(decoded->payload.empty());
}

TEST_CASE("grpc envelope - decodes non-empty uncompressed message", "[grpc]")
{
    std::array encoded{std::byte{0}, std::byte{0},    std::byte{0},   std::byte{0},
                       std::byte{2}, std::byte{0xCA}, std::byte{0xFE}};
    auto       decoded = aevox::grpc::detail::decode_unary_message(encoded, 1024);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->payload.size() == 2U);
    CHECK(decoded->payload[0] == std::byte{0xCA});
    CHECK(decoded->payload[1] == std::byte{0xFE});
}

TEST_CASE("grpc envelope - compressed flag is rejected", "[grpc]")
{
    std::array encoded{std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    auto       decoded = aevox::grpc::detail::decode_unary_message(encoded, 1024);
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error() == aevox::grpc::GrpcError::CompressionUnsupported);
}

TEST_CASE("grpc envelope - truncated header is protocol error", "[grpc]")
{
    std::array encoded{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
    auto       decoded = aevox::grpc::detail::decode_unary_message(encoded, 1024);
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error() == aevox::grpc::GrpcError::ProtocolError);
}

TEST_CASE("grpc envelope - length larger than buffer is protocol error", "[grpc]")
{
    std::array encoded{std::byte{0}, std::byte{0}, std::byte{0},
                       std::byte{0}, std::byte{2}, std::byte{0xCA}};
    auto       decoded = aevox::grpc::detail::decode_unary_message(encoded, 1024);
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error() == aevox::grpc::GrpcError::ProtocolError);
}

TEST_CASE("grpc envelope - payload over max size is MessageTooLarge", "[grpc]")
{
    std::array encoded{std::byte{0}, std::byte{0},    std::byte{0},   std::byte{0},
                       std::byte{2}, std::byte{0xCA}, std::byte{0xFE}};
    auto       decoded = aevox::grpc::detail::decode_unary_message(encoded, 1);
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error() == aevox::grpc::GrpcError::MessageTooLarge);
}

TEST_CASE("grpc envelope - extra bytes after unary message are protocol error", "[grpc]")
{
    std::array encoded{std::byte{0}, std::byte{0},    std::byte{0},   std::byte{0},
                       std::byte{1}, std::byte{0xCA}, std::byte{0xFE}};
    auto       decoded = aevox::grpc::detail::decode_unary_message(encoded, 1024);
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error() == aevox::grpc::GrpcError::ProtocolError);
}
