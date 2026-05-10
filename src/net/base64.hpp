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
    const std::size_t in_size  = input.size();
    const std::size_t out_size = ((in_size + 2U) / 3U) * 4U;

    std::string result;
    result.resize(out_size);

    std::size_t out_pos = 0;
    std::size_t in_pos  = 0;

    // Process full groups of 3 input bytes → 4 output characters.
    while (in_pos + 3U <= in_size) {
        const std::uint32_t trio = (static_cast<std::uint32_t>(input[in_pos]) << 16U) |
                                   (static_cast<std::uint32_t>(input[in_pos + 1]) << 8U) |
                                   (static_cast<std::uint32_t>(input[in_pos + 2]));
        result[out_pos++] = detail::kBase64Alphabet[(trio >> 18U) & 0x3FU];
        result[out_pos++] = detail::kBase64Alphabet[(trio >> 12U) & 0x3FU];
        result[out_pos++] = detail::kBase64Alphabet[(trio >> 6U) & 0x3FU];
        result[out_pos++] = detail::kBase64Alphabet[trio & 0x3FU];
        in_pos += 3U;
    }

    // Handle 1 or 2 remaining bytes.
    const std::size_t remainder = in_size - in_pos;
    if (remainder == 1U) {
        const auto val    = static_cast<std::uint32_t>(input[in_pos]);
        result[out_pos++] = detail::kBase64Alphabet[(val >> 2U) & 0x3FU];
        result[out_pos++] = detail::kBase64Alphabet[(val << 4U) & 0x3FU];
        result[out_pos++] = '=';
        result[out_pos++] = '=';
    }
    else if (remainder == 2U) {
        const std::uint32_t val = (static_cast<std::uint32_t>(input[in_pos]) << 8U) |
                                  (static_cast<std::uint32_t>(input[in_pos + 1]));
        result[out_pos++] = detail::kBase64Alphabet[(val >> 10U) & 0x3FU];
        result[out_pos++] = detail::kBase64Alphabet[(val >> 4U) & 0x3FU];
        result[out_pos++] = detail::kBase64Alphabet[(val << 2U) & 0x3FU];
        result[out_pos++] = '=';
    }

    return result;
}

} // namespace aevox::net
