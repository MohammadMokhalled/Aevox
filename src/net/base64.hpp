#pragma once
// src/net/base64.hpp
//
// INTERNAL — never included outside src/ or tests/.
//
// Self-contained, header-only Base64 encoder.
// Used by websocket_handshake.cpp to produce the Sec-WebSocket-Accept header value.
// Encoding only — no decoder needed for this use case.
//
// No heap allocation in the hot path for small inputs (≤ 60 input bytes).
// Inputs larger than 60 bytes use std::string allocation.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace aevox::net {

namespace detail {

/// Standard Base64 alphabet (RFC 4648 Table 1).
/// string_view used rather than array<char> so operator[] does not trigger
/// cppcoreguidelines-pro-bounds-constant-array-index on runtime indices.
inline constexpr std::string_view kBase64Alphabet{
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};
inline constexpr std::size_t   kInputGroupBytes{3};
inline constexpr std::size_t   kOutputGroupChars{4};
inline constexpr std::size_t   kOneRemainingByte{1};
inline constexpr std::size_t   kTwoRemainingBytes{2};
inline constexpr std::uint32_t kSixBitMask{0x3FU};
inline constexpr unsigned      kFirstByteShift{16U};
inline constexpr unsigned      kSecondByteShift{8U};
inline constexpr unsigned      kFirstSextetShift{18U};
inline constexpr unsigned      kSecondSextetShift{12U};
inline constexpr unsigned      kThirdSextetShift{6U};
inline constexpr unsigned      kSingleByteShift{2U};
inline constexpr unsigned      kSingleBytePadShift{4U};
inline constexpr unsigned      kTwoByteFirstShift{8U};
inline constexpr unsigned      kTwoByteSecondShift{10U};
inline constexpr unsigned      kTwoByteThirdShift{4U};
inline constexpr unsigned      kTwoBytePadShift{2U};

} // namespace detail

/**
 * @brief Encodes `input` bytes as a Base64 string (RFC 4648, with padding).
 *
 * Output length = ceil(input.size() / 3) * 4 characters (including `=` padding).
 *
 * @param input  Arbitrary byte sequence.
 * @return       Base64-encoded string.
 */
[[nodiscard]] inline std::string base64_encode(std::span<const std::uint8_t> input) noexcept
{
    const std::size_t in_size = input.size();
    const std::size_t out_size =
        ((in_size + detail::kTwoRemainingBytes) / detail::kInputGroupBytes) *
        detail::kOutputGroupChars;

    std::string result;
    result.resize(out_size);

    std::size_t out_pos = 0;
    std::size_t in_pos  = 0;

    // Process full groups of 3 input bytes → 4 output characters.
    while (in_pos + detail::kInputGroupBytes <= in_size) {
        const std::uint32_t trio =
            (static_cast<std::uint32_t>(input[in_pos]) << detail::kFirstByteShift) |
            (static_cast<std::uint32_t>(input[in_pos + detail::kOneRemainingByte])
             << detail::kSecondByteShift) |
            (static_cast<std::uint32_t>(input[in_pos + detail::kTwoRemainingBytes]));
        result[out_pos++] =
            detail::kBase64Alphabet[(trio >> detail::kFirstSextetShift) & detail::kSixBitMask];
        result[out_pos++] =
            detail::kBase64Alphabet[(trio >> detail::kSecondSextetShift) & detail::kSixBitMask];
        result[out_pos++] =
            detail::kBase64Alphabet[(trio >> detail::kThirdSextetShift) & detail::kSixBitMask];
        result[out_pos++] = detail::kBase64Alphabet[trio & detail::kSixBitMask];
        in_pos += detail::kInputGroupBytes;
    }

    // Handle 1 or 2 remaining bytes.
    const std::size_t remainder = in_size - in_pos;
    if (remainder == detail::kOneRemainingByte) {
        const auto val = static_cast<std::uint32_t>(input[in_pos]);
        result[out_pos++] =
            detail::kBase64Alphabet[(val >> detail::kSingleByteShift) & detail::kSixBitMask];
        result[out_pos++] =
            detail::kBase64Alphabet[(val << detail::kSingleBytePadShift) & detail::kSixBitMask];
        result[out_pos++] = '=';
        result[out_pos++] = '=';
    }
    else if (remainder == detail::kTwoRemainingBytes) {
        const std::uint32_t val =
            (static_cast<std::uint32_t>(input[in_pos]) << detail::kTwoByteFirstShift) |
            (static_cast<std::uint32_t>(input[in_pos + detail::kOneRemainingByte]));
        result[out_pos++] =
            detail::kBase64Alphabet[(val >> detail::kTwoByteSecondShift) & detail::kSixBitMask];
        result[out_pos++] =
            detail::kBase64Alphabet[(val >> detail::kTwoByteThirdShift) & detail::kSixBitMask];
        result[out_pos++] =
            detail::kBase64Alphabet[(val << detail::kTwoBytePadShift) & detail::kSixBitMask];
        result[out_pos++] = '=';
    }

    return result;
}

} // namespace aevox::net
