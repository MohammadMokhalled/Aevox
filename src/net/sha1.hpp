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
inline constexpr std::size_t kSha1DigestBytes{20};
using Sha1Digest = std::array<std::uint8_t, kSha1DigestBytes>;

// =============================================================================
// sha1_compute — computes SHA-1 over a single contiguous input.
// =============================================================================

namespace detail {

inline constexpr std::uint32_t kInitialH0{0x67452301U};
inline constexpr std::uint32_t kInitialH1{0xEFCDAB89U};
inline constexpr std::uint32_t kInitialH2{0x98BADCFEU};
inline constexpr std::uint32_t kInitialH3{0x10325476U};
inline constexpr std::uint32_t kInitialH4{0xC3D2E1F0U};

inline constexpr std::uint32_t kRoundConstant0{0x5A827999U};
inline constexpr std::uint32_t kRoundConstant1{0x6ED9EBA1U};
inline constexpr std::uint32_t kRoundConstant2{0x8F1BBCDCU};
inline constexpr std::uint32_t kRoundConstant3{0xCA62C1D6U};

inline constexpr std::size_t kBlockBytes{64};
inline constexpr std::size_t kInitialWordCount{16};
inline constexpr std::size_t kScheduleWordCount{80};
inline constexpr std::size_t kPaddingBufferBytes{kBlockBytes * 2U};
inline constexpr std::size_t kLengthOffsetInFinalBlock{56};
inline constexpr std::size_t kLengthBytes{8};
inline constexpr std::size_t kWordBytes{4};
inline constexpr std::size_t kScheduleOffset3{3};
inline constexpr std::size_t kScheduleOffset8{8};
inline constexpr std::size_t kScheduleOffset14{14};
inline constexpr std::size_t kScheduleOffset16{16};

inline constexpr std::size_t kRound0End{20};
inline constexpr std::size_t kRound1End{40};
inline constexpr std::size_t kRound2End{60};

inline constexpr std::uint8_t  kPaddingStartByte{0x80U};
inline constexpr std::uint32_t kBitsPerByte{8U};
inline constexpr std::uint32_t kWordBits{32U};
inline constexpr std::uint32_t kDigestRotateBits{1U};
inline constexpr std::uint32_t kMainRotateBits{5U};
inline constexpr std::uint32_t kStateRotateBits{30U};
inline constexpr std::uint32_t kByte0Shift{24U};
inline constexpr std::uint32_t kByte1Shift{16U};
inline constexpr std::uint32_t kByte2Shift{8U};

/// Left-rotate a 32-bit integer by n bits.
[[nodiscard]] inline constexpr std::uint32_t rotl32(std::uint32_t v, unsigned n) noexcept
{
    return (v << n) | (v >> (kWordBits - n));
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
    std::uint32_t h0 = detail::kInitialH0;
    std::uint32_t h1 = detail::kInitialH1;
    std::uint32_t h2 = detail::kInitialH2;
    std::uint32_t h3 = detail::kInitialH3;
    std::uint32_t h4 = detail::kInitialH4;

    // Total bit length of the message (used in padding).
    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * detail::kBitsPerByte;

    // Process data in 64-byte blocks, including the padded final block(s).
    // We operate on a local block buffer so we can append the padding without
    // modifying the caller's data.

    const std::size_t full_blocks = data.size() / detail::kBlockBytes;
    const std::size_t tail_size   = data.size() % detail::kBlockBytes;

    // Lambda: process one 512-bit (64-byte) block.
    auto process_block = [&](std::span<const std::uint8_t> block) {
        // Expand 16 words to 80 words (FIPS 180-4 §6.1.2, step 1).
        std::array<std::uint32_t, detail::kScheduleWordCount> w{};
        // Use span to avoid cppcoreguidelines-pro-bounds-constant-array-index on w[i].
        auto ws = std::span<std::uint32_t>{w};
        for (std::size_t i = 0; i < detail::kInitialWordCount; ++i) {
            const std::size_t offset = i * detail::kWordBytes;
            ws[i] = (static_cast<std::uint32_t>(block[offset]) << detail::kByte0Shift) |
                    (static_cast<std::uint32_t>(block[offset + 1U]) << detail::kByte1Shift) |
                    (static_cast<std::uint32_t>(block[offset + 2U]) << detail::kByte2Shift) |
                    (static_cast<std::uint32_t>(block[offset + 3U]));
        }
        for (std::size_t i = detail::kInitialWordCount; i < detail::kScheduleWordCount; ++i) {
            ws[i] =
                detail::rotl32(ws[i - detail::kScheduleOffset3] ^ ws[i - detail::kScheduleOffset8] ^
                                   ws[i - detail::kScheduleOffset14] ^
                                   ws[i - detail::kScheduleOffset16],
                               detail::kDigestRotateBits);
        }

        // Working variables (FIPS 180-4 §6.1.2, step 2).
        std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

        // 80 rounds (FIPS 180-4 §6.1.2, steps 3–4).
        for (std::size_t i = 0; i < detail::kScheduleWordCount; ++i) {
            std::uint32_t f{};
            std::uint32_t k{};
            if (i < detail::kRound0End) {
                f = (b & c) | ((~b) & d);
                k = detail::kRoundConstant0;
            }
            else if (i < detail::kRound1End) {
                f = b ^ c ^ d;
                k = detail::kRoundConstant1;
            }
            else if (i < detail::kRound2End) {
                f = (b & c) | (b & d) | (c & d);
                k = detail::kRoundConstant2;
            }
            else {
                f = b ^ c ^ d;
                k = detail::kRoundConstant3;
            }
            const std::uint32_t temp =
                detail::rotl32(a, detail::kMainRotateBits) + f + e + k + ws[i];
            e = d;
            d = c;
            c = detail::rotl32(b, detail::kStateRotateBits);
            b = a;
            a = temp;
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
        process_block(data.subspan(i * detail::kBlockBytes, detail::kBlockBytes));
    }

    // Build and process the padded tail block(s).
    // The tail may be split into one or two 64-byte padding blocks.
    //
    // RFC-compliant padding:
    //   1. Append 0x80 byte.
    //   2. Append 0x00 bytes until length ≡ 56 (mod 64).
    //   3. Append 8-byte big-endian bit length.
    std::array<std::uint8_t, detail::kPaddingBufferBytes> pad_buf{};
    // Use span to avoid cppcoreguidelines-pro-bounds-constant-array-index.
    auto pb = std::span<std::uint8_t>{pad_buf};
    // Copy tail bytes into pad_buf.
    for (std::size_t i = 0; i < tail_size; ++i) {
        pb[i] = data[full_blocks * detail::kBlockBytes + i];
    }
    pb[tail_size] = detail::kPaddingStartByte;

    // The bit-length goes in bytes [56..63] of the last block.
    // If the tail + 1 byte of padding overflows 56 bytes, we need two blocks.
    const std::size_t pad_blocks = (tail_size < detail::kLengthOffsetInFinalBlock) ? 1U : 2U;
    const std::size_t len_offset =
        (pad_blocks - 1U) * detail::kBlockBytes + detail::kLengthOffsetInFinalBlock;

    // Write big-endian 64-bit bit_length at len_offset.
    for (std::size_t i = 0; i < detail::kLengthBytes; ++i) {
        pb[len_offset + i] = static_cast<std::uint8_t>(
            bit_length >> ((detail::kLengthBytes - 1U - i) * detail::kBitsPerByte));
    }

    // Process padding block(s).
    for (std::size_t i = 0; i < pad_blocks; ++i) {
        process_block(pb.subspan(i * detail::kBlockBytes, detail::kBlockBytes));
    }

    // Produce the 20-byte digest (big-endian word encoding).
    Sha1Digest digest{};
    auto       dg         = std::span<std::uint8_t>{digest};
    auto       write_word = [&](std::size_t offset, std::uint32_t word) {
        dg[offset]      = static_cast<std::uint8_t>(word >> detail::kByte0Shift);
        dg[offset + 1U] = static_cast<std::uint8_t>(word >> detail::kByte1Shift);
        dg[offset + 2U] = static_cast<std::uint8_t>(word >> detail::kByte2Shift);
        dg[offset + 3U] = static_cast<std::uint8_t>(word);
    };
    write_word(0U, h0);
    write_word(detail::kWordBytes, h1);
    write_word(detail::kWordBytes * 2U, h2);
    write_word(detail::kWordBytes * 3U, h3);
    write_word(detail::kWordBytes * 4U, h4);

    return digest;
}

} // namespace aevox::net
