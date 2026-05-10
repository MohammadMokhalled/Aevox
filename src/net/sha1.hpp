#pragma once
// src/net/sha1.hpp
//
// INTERNAL — never included outside src/ or tests/.
//
// Self-contained, header-only SHA-1 implementation.
// Used exclusively by websocket_handshake.cpp for the RFC 6455 accept-key
// computation. SHA-1 is NOT used as a security primitive here — it is part
// of the WebSocket handshake protocol (RFC 6455 §4.2.2), which specifies SHA-1
// explicitly. This does not introduce a cryptographic dependency.
//
// Implementation follows FIPS 180-4.
// Tested against RFC 6455 Appendix B known-good test vector.
//
// No external dependencies. No heap allocation. constexpr where possible.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace aevox::net {

/// SHA-1 digest: 20 bytes.
using Sha1Digest = std::array<std::uint8_t, 20>;

// =============================================================================
// sha1_compute — computes SHA-1 over a single contiguous input.
// =============================================================================

namespace detail {

/// Left-rotate a 32-bit integer by n bits.
[[nodiscard]] inline constexpr std::uint32_t rotl32(std::uint32_t v, unsigned n) noexcept
{
    return (v << n) | (v >> (32U - n));
}

} // namespace detail

/**
 * @brief Computes the SHA-1 digest of `data`.
 *
 * Non-allocating. Processes the input in 64-byte blocks.
 * Handles padding and length encoding per FIPS 180-4.
 *
 * @param data  Input bytes. May be any length including zero.
 * @return      20-byte SHA-1 digest.
 */
[[nodiscard]] inline Sha1Digest sha1_compute(std::span<const std::uint8_t> data) noexcept
{
    // Initial hash values (FIPS 180-4 §5.3.1).
    std::uint32_t h0 = 0x67452301U;
    std::uint32_t h1 = 0xEFCDAB89U;
    std::uint32_t h2 = 0x98BADCFEU;
    std::uint32_t h3 = 0x10325476U;
    std::uint32_t h4 = 0xC3D2E1F0U;

    // Total bit length of the message (used in padding).
    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8U;

    // Process data in 64-byte blocks, including the padded final block(s).
    // We operate on a local block buffer so we can append the padding without
    // modifying the caller's data.

    const std::size_t full_blocks = data.size() / 64U;
    const std::size_t tail_size   = data.size() % 64U;

    // Lambda: process one 512-bit (64-byte) block.
    auto process_block = [&](const std::uint8_t* block) {
        // Expand 16 words to 80 words (FIPS 180-4 §6.1.2, step 1).
        std::array<std::uint32_t, 80> w{};
        // Use span to avoid cppcoreguidelines-pro-bounds-constant-array-index on w[i].
        auto ws = std::span<std::uint32_t>{w};
        // block is a raw pointer (passed from data()), access via span of known-size view.
        const auto bs = std::span<const std::uint8_t>{block, 64U};
        for (std::size_t i = 0; i < 16; ++i) {
            ws[i] = (static_cast<std::uint32_t>(bs[i * 4U + 0U]) << 24U) |
                    (static_cast<std::uint32_t>(bs[i * 4U + 1U]) << 16U) |
                    (static_cast<std::uint32_t>(bs[i * 4U + 2U]) << 8U) |
                    (static_cast<std::uint32_t>(bs[i * 4U + 3U]));
        }
        for (std::size_t i = 16; i < 80; ++i) {
            ws[i] = detail::rotl32(ws[i - 3] ^ ws[i - 8] ^ ws[i - 14] ^ ws[i - 16], 1U);
        }

        // Working variables (FIPS 180-4 §6.1.2, step 2).
        std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

        // 80 rounds (FIPS 180-4 §6.1.2, steps 3–4).
        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t f{};
            std::uint32_t k{};
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999U;
            }
            else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1U;
            }
            else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCU;
            }
            else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6U;
            }
            const std::uint32_t temp = detail::rotl32(a, 5U) + f + e + k + ws[i];
            e                        = d;
            d                        = c;
            c                        = detail::rotl32(b, 30U);
            b                        = a;
            a                        = temp;
        }

        // Update hash values (FIPS 180-4 §6.1.2, step 4).
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    };

    // Process full 64-byte blocks directly from the input span.
    for (std::size_t i = 0; i < full_blocks; ++i) {
        process_block(data.data() + i * 64U);
    }

    // Build and process the padded tail block(s).
    // The tail may be split into one or two 64-byte padding blocks.
    //
    // RFC-compliant padding:
    //   1. Append 0x80 byte.
    //   2. Append 0x00 bytes until length ≡ 56 (mod 64).
    //   3. Append 8-byte big-endian bit length.
    std::array<std::uint8_t, 128> pad_buf{};
    // Use span to avoid cppcoreguidelines-pro-bounds-constant-array-index.
    auto pb = std::span<std::uint8_t>{pad_buf};
    // Copy tail bytes into pad_buf.
    for (std::size_t i = 0; i < tail_size; ++i) {
        pb[i] = data[full_blocks * 64U + i];
    }
    pb[tail_size] = 0x80U; // append 1-bit

    // The bit-length goes in bytes [56..63] of the last block.
    // If the tail + 1 byte of padding overflows 56 bytes, we need two blocks.
    const std::size_t pad_blocks = (tail_size < 56U) ? 1U : 2U;
    const std::size_t len_offset = (pad_blocks - 1U) * 64U + 56U;

    // Write big-endian 64-bit bit_length at len_offset.
    for (std::size_t i = 0; i < 8; ++i) {
        pb[len_offset + i] = static_cast<std::uint8_t>(bit_length >> ((7U - i) * 8U));
    }

    // Process padding block(s).
    for (std::size_t i = 0; i < pad_blocks; ++i) {
        process_block(pad_buf.data() + i * 64U);
    }

    // Produce the 20-byte digest (big-endian word encoding).
    Sha1Digest digest{};
    auto       dg         = std::span<std::uint8_t>{digest};
    auto       write_word = [&](std::size_t offset, std::uint32_t word) {
        dg[offset + 0U] = static_cast<std::uint8_t>(word >> 24U);
        dg[offset + 1U] = static_cast<std::uint8_t>(word >> 16U);
        dg[offset + 2U] = static_cast<std::uint8_t>(word >> 8U);
        dg[offset + 3U] = static_cast<std::uint8_t>(word);
    };
    write_word(0, h0);
    write_word(4, h1);
    write_word(8, h2);
    write_word(12, h3);
    write_word(16, h4);

    return digest;
}

} // namespace aevox::net
