#pragma once

#include <cstdint>
#include <string_view>

namespace aevox {

/**
 * @brief Broad category for Aevox error values.
 *
 * `ErrorCategory` is a lightweight classifier for logging, documentation,
 * metrics, and generic user code. It does not replace module-specific error
 * codes; callers should branch on module error enums when they need precise
 * handling.
 *
 * @note Thread-safety: stateless enum, inherently thread-safe.
 * @note Ownership: no dynamic storage.
 */
enum class ErrorCategory : std::uint8_t
{
    Io,            ///< Socket, stream, cancellation, timeout, or executor failure.
    Protocol,      ///< HTTP/WebSocket protocol violation or malformed input.
    Parse,         ///< Structured payload or config syntax parsing failure.
    Validation,    ///< Value is syntactically valid but semantically invalid.
    NotFound,      ///< Requested route, parameter, header, file, or key is absent.
    Serialization, ///< Structured value could not be serialized.
    State,         ///< Operation is invalid for the current object/server state.
    Unknown,       ///< Fallback for unexpected or intentionally opaque failures.
};

/**
 * @brief Returns a stable text label for an ErrorCategory.
 *
 * @param category  The category to describe.
 * @return Static string literal with process lifetime.
 * @note Thread-safety: safe to call concurrently.
 */
[[nodiscard]] std::string_view to_string(ErrorCategory category) noexcept;

} // namespace aevox
