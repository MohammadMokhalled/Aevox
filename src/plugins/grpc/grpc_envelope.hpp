#pragma once
// Internal gRPC message envelope helpers.

#include <aevox/plugins/grpc.hpp>

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace aevox::grpc::detail {

struct DecodedMessage
{
    std::vector<std::byte> payload;
};

[[nodiscard]] std::expected<DecodedMessage, GrpcError> decode_unary_message(
    std::span<const std::byte> bytes, std::size_t max_message_size);

[[nodiscard]] std::expected<std::vector<std::byte>, GrpcError> encode_unary_message(
    std::span<const std::byte> payload, std::size_t max_message_size);

} // namespace aevox::grpc::detail
