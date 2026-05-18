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
#include <string_view>

namespace aevox::detail {

/**
 * @brief Parsed components of a W3C Trace Context traceparent header.
 *
 * @note Thread-safety: value type, inherently thread-safe after construction.
 * @note Ownership: no dynamic storage; all fields are fixed-size arrays or scalars.
 */
struct TraceParent
{
    std::array<char, 32> trace_id{};  ///< 32 lowercase hex characters.
    std::array<char, 16> parent_id{}; ///< 16 lowercase hex characters.
    std::uint8_t         flags{0};    ///< Trace flags byte (bit 0 = sampled).
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
    if (header.size() != 55) {
        return std::nullopt;
    }

    // Separator positions: index 2, 35, 52
    if (header[2] != '-' || header[35] != '-' || header[52] != '-') {
        return std::nullopt;
    }

    // Version check: must be "00"
    if (header[0] != '0' || header[1] != '0') {
        return std::nullopt;
    }

    // Helper: check if a char is a lowercase hex digit.
    const auto is_lower_hex = [](char c) noexcept -> bool {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    };

    // Validate and extract trace-id (indices 3..34, 32 chars)
    TraceParent result;
    bool        trace_id_all_zero = true;
    const char* src               = header.data() + 3;
    char*       dst               = result.trace_id.data();
    for (std::size_t i = 0; i < 32; ++i, ++src, ++dst) {
        const char c = *src;
        if (c >= 'A' && c <= 'F') {
            *dst              = static_cast<char>(c - 'A' + 'a');
            trace_id_all_zero = false;
        }
        else if (is_lower_hex(c)) {
            *dst = c;
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
    src                     = header.data() + 36;
    dst                     = result.parent_id.data();
    for (std::size_t i = 0; i < 16; ++i, ++src, ++dst) {
        const char c = *src;
        if (c >= 'A' && c <= 'F') {
            *dst               = static_cast<char>(c - 'A' + 'a');
            parent_id_all_zero = false;
        }
        else if (is_lower_hex(c)) {
            *dst = c;
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
    for (std::size_t i = 0; i < 2; ++i) {
        const char c = header[53 + i];
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
            return static_cast<std::uint8_t>(c - 'a' + 10);
        }
        if (c >= 'A' && c <= 'F') {
            return static_cast<std::uint8_t>(c - 'A' + 10);
        }
        return 0; // unreachable — validated above
    };
    result.flags =
        static_cast<std::uint8_t>((hex_nibble(header[53]) << 4) | hex_nibble(header[54]));

    return result;
}

} // namespace aevox::detail
