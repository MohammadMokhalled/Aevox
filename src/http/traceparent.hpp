#pragma once
// src/http/traceparent.hpp
//
// INTERNAL — W3C Trace Context Level 1 traceparent parsing.
//
// Grammar (W3C Trace Context, Version 1):
//   traceparent = version "-" trace-id "-" parent-id "-" trace-flags
//   version     = 2HEXDIG   ; only "00" is defined
//   trace-id    = 32HEXDIG  ; must not be all zeros
//   parent-id   = 16HEXDIG  ; must not be all zeros
//   trace-flags = 2HEXDIG   ; value not constrained by this version
//
// Only version "00" is accepted. Unknown versions are rejected per spec §3.2.
//
// Design: Tasks/architecture/AEV-012-arch.md §5.1

#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace aevox::detail {

inline constexpr std::size_t  kTraceParentLength{55};
inline constexpr std::size_t  kTraceVersion0Index{0};
inline constexpr std::size_t  kTraceVersion1Index{1};
inline constexpr std::size_t  kTraceVersionSeparatorIndex{2};
inline constexpr std::size_t  kTraceIdStartIndex{3};
inline constexpr std::size_t  kTraceIdLength{32};
inline constexpr std::size_t  kTraceIdSeparatorIndex{35};
inline constexpr std::size_t  kParentIdStartIndex{36};
inline constexpr std::size_t  kParentIdLength{16};
inline constexpr std::size_t  kParentIdSeparatorIndex{52};
inline constexpr std::size_t  kTraceFlagsStartIndex{53};
inline constexpr std::size_t  kTraceFlagsLength{2};
inline constexpr std::uint8_t kTraceHexBase{10};
inline constexpr std::uint8_t kTraceHighNibbleShift{4};

/**
 * @brief Parsed components of a W3C Trace Context traceparent header.
 *
 * @note Thread-safety: value type, inherently thread-safe after construction.
 * @note Ownership: no dynamic storage; all fields are fixed-size arrays or scalars.
 */
struct TraceParent
{
    std::array<char, kTraceIdLength>  trace_id{};  ///< 32 lowercase hex characters.
    std::array<char, kParentIdLength> parent_id{}; ///< 16 lowercase hex characters.
    std::uint8_t                      flags{0};    ///< Trace flags byte (bit 0 = sampled).
};

/**
 * @brief Parses a W3C Trace Context traceparent header value.
 *
 * Validates the header against the W3C Trace Context Level 1 specification
 * and returns the decomposed components on success.
 *
 * Validation rules:
 *   - Must be exactly 55 characters: "00-" + 32 + "-" + 16 + "-" + 2
 *   - Version must be "00" (only defined version)
 *   - trace-id must be 32 hex digits, not all zeros
 *   - parent-id must be 16 hex digits, not all zeros
 *   - trace-flags must be 2 hex digits (value not constrained)
 *
 * @param header  The raw traceparent header value (no leading/trailing whitespace).
 * @return        Parsed TraceParent on success, or std::nullopt if the header
 *                is absent, syntactically invalid, or uses an unsupported version.
 * @note          noexcept — no allocation, no I/O.
 * @note          Thread-safety: pure function, safe to call concurrently.
 */
[[nodiscard]] inline std::optional<TraceParent> parse_traceparent(std::string_view header) noexcept
{
    // Length check: "00-<32>-<16>-<2>" = 2 + 1 + 32 + 1 + 16 + 1 + 2 = 55
    if (header.size() != kTraceParentLength) {
        return std::nullopt;
    }

    // Separator positions: index 2, 35, 52
    if (header[kTraceVersionSeparatorIndex] != '-' || header[kTraceIdSeparatorIndex] != '-' ||
        header[kParentIdSeparatorIndex] != '-')
    {
        return std::nullopt;
    }

    // Version check: must be "00"
    if (header[kTraceVersion0Index] != '0' || header[kTraceVersion1Index] != '0') {
        return std::nullopt;
    }

    // Helper: check if a char is a lowercase hex digit.
    const auto is_lower_hex = [](char c) noexcept -> bool {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    };

    // Validate and extract trace-id (indices 3..34, 32 chars)
    TraceParent result;
    bool        trace_id_all_zero = true;
    auto        trace_id          = std::span<char>{result.trace_id};
    for (std::size_t i = 0; i < kTraceIdLength; ++i) {
        const char c = header[kTraceIdStartIndex + i];
        if (c >= 'A' && c <= 'F') {
            trace_id[i]       = static_cast<char>(c - 'A' + 'a');
            trace_id_all_zero = false;
        }
        else if (is_lower_hex(c)) {
            trace_id[i] = c;
            if (c != '0') {
                trace_id_all_zero = false;
            }
        }
        else {
            return std::nullopt;
        }
    }
    if (trace_id_all_zero) {
        return std::nullopt;
    }

    // Validate and extract parent-id (indices 36..51, 16 chars)
    bool parent_id_all_zero = true;
    auto parent_id          = std::span<char>{result.parent_id};
    for (std::size_t i = 0; i < kParentIdLength; ++i) {
        const char c = header[kParentIdStartIndex + i];
        if (c >= 'A' && c <= 'F') {
            parent_id[i]       = static_cast<char>(c - 'A' + 'a');
            parent_id_all_zero = false;
        }
        else if (is_lower_hex(c)) {
            parent_id[i] = c;
            if (c != '0') {
                parent_id_all_zero = false;
            }
        }
        else {
            return std::nullopt;
        }
    }
    if (parent_id_all_zero) {
        return std::nullopt;
    }

    // Validate trace-flags (indices 53..54, 2 hex chars)
    for (std::size_t i = 0; i < kTraceFlagsLength; ++i) {
        const char c = header[kTraceFlagsStartIndex + i];
        if (!(is_lower_hex(c) || (c >= 'A' && c <= 'F'))) {
            return std::nullopt;
        }
    }

    // Parse flags byte from hex.
    // Manual hex-to-nibble to avoid <charconv> dependency overhead in a header.
    const auto hex_nibble = [](char c) noexcept -> std::uint8_t {
        if (c >= '0' && c <= '9') {
            return static_cast<std::uint8_t>(c - '0');
        }
        if (c >= 'a' && c <= 'f') {
            return static_cast<std::uint8_t>(c - 'a' + kTraceHexBase);
        }
        if (c >= 'A' && c <= 'F') {
            return static_cast<std::uint8_t>(c - 'A' + kTraceHexBase);
        }
        return 0; // unreachable — validated above
    };
    result.flags = static_cast<std::uint8_t>(
        (hex_nibble(header[kTraceFlagsStartIndex]) << kTraceHighNibbleShift) |
        hex_nibble(header[kTraceFlagsStartIndex + 1U]));

    return result;
}

} // namespace aevox::detail
