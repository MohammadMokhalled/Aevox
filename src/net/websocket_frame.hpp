#pragma once
// src/net/websocket_frame.hpp
//
// INTERNAL — never included outside src/ or tests/.
//
// RFC 6455 frame parser and emitter.
// Pure functions — no shared state, no I/O, fully thread-safe.
//
// Design: Tasks/architecture/AEV-010-arch.md §4.3

#include <aevox/websocket_error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <vector>

namespace aevox::net {

// =============================================================================
// Opcode — RFC 6455 §5.2 frame opcodes.
// =============================================================================

enum class Opcode : std::uint8_t
{
    Continuation = 0x0, ///< Fragmented message continuation — rejected in v0.2.
    Text         = 0x1, ///< UTF-8 text frame.
    Binary       = 0x2, ///< Binary data frame.
    Close        = 0x8, ///< Connection close frame.
    Ping         = 0x9, ///< Ping (keepalive probe).
    Pong         = 0xA, ///< Pong (keepalive reply).
};

// =============================================================================
// ParsedFrame — result of a successful frame parse.
// =============================================================================

/// A fully parsed and unmasked WebSocket frame.
struct ParsedFrame
{
    Opcode                 opcode;  ///< Frame opcode.
    bool                   fin;     ///< FIN bit set (complete single-frame message).
    std::vector<std::byte> payload; ///< Unmasked payload bytes (owned).
};

/// Returned by parse_frame() on success: the frame and how many bytes were consumed.
struct ParseResult
{
    ParsedFrame frame;
    std::size_t bytes_consumed{}; ///< Number of bytes consumed from the start of `input`.
};

enum class ParseFrameErrorKind : std::uint8_t
{
    Incomplete,
    Protocol,
};

struct ParseFrameError
{
    ParseFrameError(ParseFrameErrorKind error_kind, aevox::WebSocketError websocket_error) noexcept
        : kind_{error_kind}, error_{std::move(websocket_error)}
    {}

    [[nodiscard]] ParseFrameErrorKind kind() const noexcept
    {
        return kind_;
    }

    [[nodiscard]] const aevox::WebSocketError& error() const noexcept
    {
        return error_;
    }

private:
    ParseFrameErrorKind   kind_{};
    aevox::WebSocketError error_;
};

// =============================================================================
// parse_frame — parse one RFC 6455 frame from a byte buffer.
// =============================================================================

/**
 * @brief Parses one RFC 6455 frame from the front of `input`.
 *
 * Validates the mask bit (client frames must be masked, server frames must not),
 * enforces the 64-bit payload length limit, and unmasks the payload if masked.
 *
 * Reserved opcodes (0x3–0x7, 0xB–0xF) return `protocol_error`.
 * Fragmented frames (FIN=0) return `protocol_error` (not supported in v0.2).
 * Unmaked client frames (MASK=0) return `protocol_error` (RFC 6455 §5.3).
 *
 * @param input             Raw bytes starting at a frame boundary. May contain
 *                          extra bytes (e.g. a subsequent frame) — only
 *                          `bytes_consumed` bytes are consumed.
 * @param max_payload_bytes Maximum allowed payload size. If the payload length
 *                          field exceeds this value, `frame_too_large` is returned.
 * @param expect_masked     Whether to require the MASK bit to be set. `true`
 *                          for client-to-server frames (RFC 6455 §5.3 mandate).
 *                          Defaults to `true`.
 * @return                  `ParseResult` on success (frame + bytes consumed).
 *                          `WebSocketError` on protocol violation.
 */
[[nodiscard]] std::expected<ParseResult, aevox::WebSocketError> parse_frame(
    std::span<const std::byte> input, std::size_t max_payload_bytes,
    bool expect_masked = true) noexcept;

[[nodiscard]] std::expected<ParseResult, ParseFrameError> parse_frame_detailed(
    std::span<const std::byte> input, std::size_t max_payload_bytes,
    bool expect_masked = true) noexcept;

// =============================================================================
// emit_frame — serialize a server-to-client frame (unmasked).
// =============================================================================

/**
 * @brief Serializes a server-to-client WebSocket frame.
 *
 * Servers MUST NOT mask frames (RFC 6455 §5.3). The MASK bit is always 0.
 * Applies the correct payload length encoding (7-bit, 16-bit extended, or
 * 64-bit extended) based on `payload.size()`.
 *
 * @param opcode   Frame opcode.
 * @param fin      Whether to set the FIN bit.
 * @param payload  Payload bytes (non-owning view — copied into the output).
 * @return         Serialized frame bytes (owned).
 */
[[nodiscard]] std::vector<std::byte> emit_frame(Opcode opcode, bool fin,
                                                std::span<const std::byte> payload);

// =============================================================================
// emit_close_frame — convenience helper for Close frames.
// =============================================================================

/**
 * @brief Serializes an RFC 6455 Close frame.
 *
 * The close status code is encoded as a 2-byte big-endian integer followed by
 * the optional UTF-8 reason string (RFC 6455 §5.5.1). If `reason` is longer
 * than 123 bytes, it is silently truncated to 123 bytes (RFC 6455 limit of
 * 125-byte control frame payload minus 2 bytes for the code).
 *
 * @param code    RFC 6455 close status code (e.g. 1000 for normal closure).
 * @param reason  Optional UTF-8 reason string.
 * @return        Serialized Close frame bytes (FIN=1, opcode=0x8, unmasked).
 */
[[nodiscard]] std::vector<std::byte> emit_close_frame(std::uint16_t    code,
                                                      std::string_view reason = {});

} // namespace aevox::net
