// src/net/websocket_frame.cpp
//
// INTERNAL — RFC 6455 frame parser and emitter implementation.
// No Asio types. No exceptions.
//
// Design: Tasks/architecture/AEV-010-arch.md §4.3

#include "net/websocket_frame.hpp"

#include <aevox/websocket_error.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace aevox::net {

namespace {

constexpr std::size_t   kBaseHeaderBytes{2};
constexpr std::size_t   kExtended16Bytes{2};
constexpr std::size_t   kExtended64Bytes{8};
constexpr std::size_t   kMaskKeyBytes{4};
constexpr std::uint8_t  kFinBit{0x80U};
constexpr std::uint8_t  kMaskBit{0x80U};
constexpr std::uint8_t  kOpcodeMask{0x0FU};
constexpr std::uint8_t  kRsvMask{0x70U};
constexpr std::uint8_t  kPayloadLengthMask{0x7FU};
constexpr std::uint8_t  kPayload16Marker{126U};
constexpr std::uint8_t  kPayload64Marker{127U};
constexpr std::uint8_t  kServerMaskBit{0x00U};
constexpr std::uint8_t  kByteMask{0xFFU};
constexpr std::uint64_t kPayload64MsbMask{0x8000000000000000ULL};
constexpr unsigned      kByteBits{8U};
constexpr std::size_t   kCloseCodeBytes{kExtended16Bytes};
constexpr std::size_t   kMaxControlPayloadBytes{125};
constexpr std::size_t   kMaxCloseReasonBytes{kMaxControlPayloadBytes - kCloseCodeBytes};

/// Returns true if the opcode is a valid known opcode.
[[nodiscard]] bool is_known_opcode(std::uint8_t raw) noexcept
{
    switch (raw) {
        case static_cast<std::uint8_t>(Opcode::Continuation):
        case static_cast<std::uint8_t>(Opcode::Text):
        case static_cast<std::uint8_t>(Opcode::Binary):
        case static_cast<std::uint8_t>(Opcode::Close):
        case static_cast<std::uint8_t>(Opcode::Ping):
        case static_cast<std::uint8_t>(Opcode::Pong):
            return true;
        default:
            return false;
    }
}

/// Appends a big-endian 16-bit value to a vector.
void push_be16(std::vector<std::byte>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::byte>(value >> kByteBits));
    out.push_back(static_cast<std::byte>(value & kByteMask));
}

/// Appends a big-endian 64-bit value to a vector.
void push_be64(std::vector<std::byte>& out, std::uint64_t value)
{
    for (std::size_t i = 0; i < kExtended64Bytes; ++i) {
        const auto shift = static_cast<unsigned>((kExtended64Bytes - 1U - i) * kByteBits);
        out.push_back(static_cast<std::byte>((value >> shift) & kByteMask));
    }
}

} // anonymous namespace

// =============================================================================
// parse_frame
// =============================================================================

std::expected<ParseResult, ParseFrameError> parse_frame_detailed(std::span<const std::byte> input,
                                                                 std::size_t max_payload_bytes,
                                                                 bool        expect_masked) noexcept
{
    // Minimum frame header: 2 bytes.
    if (input.size() < kBaseHeaderBytes) {
        return std::unexpected(ParseFrameError{
            ParseFrameErrorKind::Incomplete,
            aevox::WebSocketError{aevox::WebSocketErrorCode::ProtocolError,
                                  "Frame too short: need at least 2 header bytes"}});
    }

    const auto byte0 = static_cast<std::uint8_t>(input[0]);
    const auto byte1 = static_cast<std::uint8_t>(input[1]);

    // Parse FIN bit and opcode from byte 0.
    const bool fin        = (byte0 & kFinBit) != 0U;
    const auto raw_opcode = static_cast<std::uint8_t>(byte0 & kOpcodeMask);

    // RSV bits (1-3) must be 0 (no extensions negotiated in v0.2).
    if ((byte0 & kRsvMask) != 0U) {
        return std::unexpected(ParseFrameError{
            ParseFrameErrorKind::Protocol,
            aevox::WebSocketError{aevox::WebSocketErrorCode::ProtocolError,
                                  "Non-zero RSV bits without extension negotiation"}});
    }

    // Validate opcode.
    if (!is_known_opcode(raw_opcode)) {
        return std::unexpected(
            ParseFrameError{ParseFrameErrorKind::Protocol,
                            aevox::WebSocketError{aevox::WebSocketErrorCode::ProtocolError,
                                                  "Reserved or unknown opcode"}});
    }

    const auto opcode = static_cast<Opcode>(raw_opcode);

    // Continuation frame (fragmentation) is rejected in v0.2.
    if (opcode == Opcode::Continuation) {
        return std::unexpected(ParseFrameError{
            ParseFrameErrorKind::Protocol,
            aevox::WebSocketError{
                aevox::WebSocketErrorCode::ProtocolError,
                "Fragmented messages (FIN=0, continuation frames) not supported in v0.2"}});
    }

    // Control frames (Close, Ping, Pong) must have FIN=1 (RFC 6455 §5.5).
    if (!fin && (opcode == Opcode::Close || opcode == Opcode::Ping || opcode == Opcode::Pong)) {
        return std::unexpected(ParseFrameError{
            ParseFrameErrorKind::Protocol,
            aevox::WebSocketError{aevox::WebSocketErrorCode::ProtocolError,
                                  "Control frame must not be fragmented (FIN must be 1)"}});
    }

    // Fragmented data frames (FIN=0) are also rejected in v0.2.
    if (!fin && (opcode == Opcode::Text || opcode == Opcode::Binary)) {
        return std::unexpected(ParseFrameError{
            ParseFrameErrorKind::Protocol,
            aevox::WebSocketError{aevox::WebSocketErrorCode::ProtocolError,
                                  "Fragmented messages (FIN=0) not supported in v0.2"}});
    }

    // Parse MASK bit and 7-bit payload length from byte 1.
    const bool masked = (byte1 & kMaskBit) != 0U;
    const auto len7   = static_cast<std::uint8_t>(byte1 & kPayloadLengthMask);

    // Enforce mask requirement (RFC 6455 §5.3: client must mask, server must not).
    if (expect_masked && !masked) {
        return std::unexpected(ParseFrameError{
            ParseFrameErrorKind::Protocol,
            aevox::WebSocketError{aevox::WebSocketErrorCode::ProtocolError,
                                  "Client frame must have MASK bit set (RFC 6455 §5.3)"}});
    }
    if (!expect_masked && masked) {
        return std::unexpected(ParseFrameError{
            ParseFrameErrorKind::Protocol,
            aevox::WebSocketError{aevox::WebSocketErrorCode::ProtocolError,
                                  "Server frame must not have MASK bit set (RFC 6455 §5.3)"}});
    }

    // Determine header size and actual payload length.
    std::size_t   header_size   = kBaseHeaderBytes + (masked ? kMaskKeyBytes : 0U);
    std::uint64_t payload_len64 = 0;

    if (len7 < kPayload16Marker) {
        payload_len64 = len7;
    }
    else if (len7 == kPayload16Marker) {
        // 16-bit extended payload length.
        header_size += kExtended16Bytes;
        if (input.size() < header_size - (masked ? kMaskKeyBytes : 0U)) {
            return std::unexpected(ParseFrameError{
                ParseFrameErrorKind::Incomplete,
                aevox::WebSocketError{aevox::WebSocketErrorCode::ProtocolError,
                                      "Frame too short for 16-bit extended payload length"}});
        }
        payload_len64 =
            (static_cast<std::uint64_t>(static_cast<std::uint8_t>(input[kBaseHeaderBytes]))
             << kByteBits) |
            (static_cast<std::uint64_t>(static_cast<std::uint8_t>(input[kBaseHeaderBytes + 1U])));
    }
    else {
        // len7 == 127: 64-bit extended payload length.
        header_size += kExtended64Bytes;
        if (input.size() < header_size - (masked ? kMaskKeyBytes : 0U)) {
            return std::unexpected(ParseFrameError{
                ParseFrameErrorKind::Incomplete,
                aevox::WebSocketError{aevox::WebSocketErrorCode::ProtocolError,
                                      "Frame too short for 64-bit extended payload length"}});
        }
        payload_len64 = 0;
        for (std::size_t i = 0; i < kExtended64Bytes; ++i) {
            payload_len64 =
                (payload_len64 << kByteBits) |
                static_cast<std::uint64_t>(static_cast<std::uint8_t>(input[kBaseHeaderBytes + i]));
        }
        // RFC 6455 §5.2: most-significant bit of 64-bit length must be 0.
        if ((payload_len64 & kPayload64MsbMask) != 0U) {
            return std::unexpected(ParseFrameError{
                ParseFrameErrorKind::Protocol,
                aevox::WebSocketError{aevox::WebSocketErrorCode::ProtocolError,
                                      "64-bit payload length MSB must be 0 (RFC 6455 §5.2)"}});
        }
    }

    // Now account for masking key bytes (if present).
    // The masking key follows the payload length bytes.
    // Re-compute header_size to include masking key bytes.
    // (header_size was computed with masked bytes already included above for len7 < 126 case)
    // Let's recompute cleanly:
    std::size_t ext_len_bytes = 0;
    if (len7 == kPayload16Marker) {
        ext_len_bytes = kExtended16Bytes;
    }
    else if (len7 == kPayload64Marker) {
        ext_len_bytes = kExtended64Bytes;
    }
    const std::size_t mask_bytes    = masked ? kMaskKeyBytes : 0U;
    const std::size_t actual_header = kBaseHeaderBytes + ext_len_bytes + mask_bytes;

    // Enforce payload size limit.
    if (payload_len64 > static_cast<std::uint64_t>(max_payload_bytes)) {
        return std::unexpected(
            ParseFrameError{ParseFrameErrorKind::Protocol,
                            aevox::WebSocketError{aevox::WebSocketErrorCode::FrameTooLarge,
                                                  "Payload length exceeds max_body_size limit"}});
    }

    const auto        payload_len = static_cast<std::size_t>(payload_len64);
    const std::size_t total_frame = actual_header + payload_len;

    // Ensure the input buffer contains the full frame.
    if (input.size() < total_frame) {
        return std::unexpected(ParseFrameError{
            ParseFrameErrorKind::Incomplete,
            aevox::WebSocketError{aevox::WebSocketErrorCode::ProtocolError,
                                  "Input buffer does not contain a complete frame"}});
    }

    // Extract masking key (if present).
    std::array<std::uint8_t, kMaskKeyBytes> mask_key{};
    if (masked) {
        const std::size_t mask_offset = kBaseHeaderBytes + ext_len_bytes;
        auto              mk          = std::span<std::uint8_t>{mask_key};
        for (std::size_t i = 0; i < kMaskKeyBytes; ++i) {
            mk[i] = static_cast<std::uint8_t>(input[mask_offset + i]);
        }
    }

    // Extract and unmask payload.
    const std::size_t      payload_offset = actual_header;
    std::vector<std::byte> payload(payload_len);

    if (masked) {
        const auto mk = std::span<const std::uint8_t>{mask_key};
        for (std::size_t i = 0; i < payload_len; ++i) {
            payload[i] = static_cast<std::byte>(
                static_cast<std::uint8_t>(input[payload_offset + i]) ^ mk[i % kMaskKeyBytes]);
        }
    }
    else {
        for (std::size_t i = 0; i < payload_len; ++i) {
            payload[i] = input[payload_offset + i];
        }
    }

    return ParseResult{.frame =
                           ParsedFrame{.opcode = opcode, .fin = fin, .payload = std::move(payload)},
                       .bytes_consumed = total_frame};
}

std::expected<ParseResult, aevox::WebSocketError> parse_frame(std::span<const std::byte> input,
                                                              std::size_t max_payload_bytes,
                                                              bool        expect_masked) noexcept
{
    auto result = parse_frame_detailed(input, max_payload_bytes, expect_masked);
    if (!result) {
        return std::unexpected(result.error().error());
    }
    return std::move(*result);
}

// =============================================================================
// emit_frame
// =============================================================================

std::vector<std::byte> emit_frame(Opcode opcode, bool fin, std::span<const std::byte> payload)
{
    const std::size_t payload_size = payload.size();

    // Determine extended payload length encoding.
    std::size_t ext_len_bytes = 0;
    if (payload_size >= kPayload16Marker &&
        payload_size <= std::numeric_limits<std::uint16_t>::max())
    {
        ext_len_bytes = kExtended16Bytes;
    }
    else if (payload_size > std::numeric_limits<std::uint16_t>::max()) {
        ext_len_bytes = kExtended64Bytes;
    }

    const std::size_t      header_size = kBaseHeaderBytes + ext_len_bytes;
    std::vector<std::byte> frame;
    frame.reserve(header_size + payload_size);

    // Byte 0: FIN bit + opcode.
    const std::uint8_t byte0 = (fin ? kFinBit : kServerMaskBit) | static_cast<std::uint8_t>(opcode);
    frame.push_back(static_cast<std::byte>(byte0));

    // Byte 1: MASK=0 (servers must not mask) + payload length.
    if (payload_size < kPayload16Marker) {
        frame.push_back(static_cast<std::byte>(payload_size));
    }
    else if (payload_size <= std::numeric_limits<std::uint16_t>::max()) {
        frame.push_back(static_cast<std::byte>(kPayload16Marker));
        push_be16(frame, static_cast<std::uint16_t>(payload_size));
    }
    else {
        frame.push_back(static_cast<std::byte>(kPayload64Marker));
        push_be64(frame, static_cast<std::uint64_t>(payload_size));
    }

    // Append payload (unmasked — server frames must not be masked).
    frame.insert(frame.end(), payload.begin(), payload.end());

    return frame;
}

// =============================================================================
// emit_close_frame
// =============================================================================

std::vector<std::byte> emit_close_frame(std::uint16_t code, std::string_view reason)
{
    const std::size_t reason_len = std::min(reason.size(), kMaxCloseReasonBytes);

    std::vector<std::byte> payload;
    payload.reserve(kCloseCodeBytes + reason_len);
    payload.push_back(static_cast<std::byte>(code >> kByteBits));
    payload.push_back(static_cast<std::byte>(code & kByteMask));
    for (std::size_t i = 0; i < reason_len; ++i) {
        payload.push_back(static_cast<std::byte>(static_cast<unsigned char>(reason[i])));
    }

    return emit_frame(Opcode::Close, true, std::span<const std::byte>{payload});
}

} // namespace aevox::net
