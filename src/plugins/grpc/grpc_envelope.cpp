#include "grpc_envelope.hpp"

#include <aevox/plugins/grpc.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <span>
#include <vector>

namespace aevox::grpc::detail {

namespace {

constexpr std::size_t   kEnvelopeSize{5};
constexpr std::byte     kUncompressedFlag{0};
constexpr std::uint32_t kOctetMask{0xFFU};
constexpr unsigned      kByte3Shift{24U};
constexpr unsigned      kByte2Shift{16U};
constexpr unsigned      kByte1Shift{8U};

[[nodiscard]] std::uint32_t read_big_endian_u32(std::span<const std::byte, 4> bytes) noexcept
{
    auto b0 = static_cast<std::uint32_t>(bytes[0]);
    auto b1 = static_cast<std::uint32_t>(bytes[1]);
    auto b2 = static_cast<std::uint32_t>(bytes[2]);
    auto b3 = static_cast<std::uint32_t>(bytes[3]);
    return (b0 << kByte3Shift) | (b1 << kByte2Shift) | (b2 << kByte1Shift) | b3;
}

void append_big_endian_u32(std::vector<std::byte>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::byte>((value >> kByte3Shift) & kOctetMask));
    out.push_back(static_cast<std::byte>((value >> kByte2Shift) & kOctetMask));
    out.push_back(static_cast<std::byte>((value >> kByte1Shift) & kOctetMask));
    out.push_back(static_cast<std::byte>(value & kOctetMask));
}

} // namespace

std::expected<DecodedMessage, GrpcError> decode_unary_message(std::span<const std::byte> bytes,
                                                              std::size_t max_message_size)
{
    if (bytes.size() < kEnvelopeSize) {
        return std::unexpected{GrpcError::ProtocolError};
    }
    if (bytes.front() != kUncompressedFlag) {
        return std::unexpected{GrpcError::CompressionUnsupported};
    }

    const auto length = read_big_endian_u32(std::span<const std::byte, 4>{bytes.subspan<1, 4>()});
    if (length > max_message_size) {
        return std::unexpected{GrpcError::MessageTooLarge};
    }

    const auto payload_size = static_cast<std::size_t>(length);
    if (bytes.size() < kEnvelopeSize + payload_size) {
        return std::unexpected{GrpcError::ProtocolError};
    }
    if (bytes.size() != kEnvelopeSize + payload_size) {
        return std::unexpected{GrpcError::ProtocolError};
    }

    DecodedMessage decoded;
    decoded.payload.resize(payload_size);
    std::ranges::copy(bytes.subspan(kEnvelopeSize), decoded.payload.begin());
    return decoded;
}

std::expected<std::vector<std::byte>, GrpcError> encode_unary_message(
    std::span<const std::byte> payload, std::size_t max_message_size)
{
    if (payload.size() > max_message_size) {
        return std::unexpected{GrpcError::MessageTooLarge};
    }

    std::vector<std::byte> encoded;
    encoded.reserve(kEnvelopeSize + payload.size());
    encoded.push_back(kUncompressedFlag);
    append_big_endian_u32(encoded, static_cast<std::uint32_t>(payload.size()));
    std::ranges::copy(payload, std::back_inserter(encoded));
    return encoded;
}

} // namespace aevox::grpc::detail
