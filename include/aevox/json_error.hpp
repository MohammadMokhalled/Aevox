#pragma once
// include/aevox/json_error.hpp
//
// Structured error type for all JSON operations.
// No third-party library types. Standard C++ only.

#include <aevox/error.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace aevox {

/**
 * @brief Error codes for JSON parse and serialization failures.
 *
 * Returned by `JsonError::code()` and accepted by `to_string(JsonErrorCode)`.
 * Values are stable discriminants for branching; use `JsonError::message()` for
 * human-readable details.
 *
 * @note Thread-safety: stateless enum — inherently thread-safe.
 */
enum class JsonErrorCode : std::uint8_t
{
    ParseError,          ///< Input is not valid JSON syntax.
    TypeMismatch,        ///< JSON value type does not match the target C++ field type.
    MissingField,        ///< Required field is absent from the JSON payload.
    SerializationFailed, ///< A C++ value could not be serialized as JSON.
    InvalidUtf8,         ///< JSON text contains invalid UTF-8.
    Unknown,             ///< Backend did not provide a more specific error code.
};

/**
 * @brief Returns a human-readable description of a JsonErrorCode value.
 *
 * @param code  The JSON error code to describe.
 * @return Static string literal with process lifetime.
 * @note Thread-safety: safe to call concurrently.
 */
[[nodiscard]] std::string_view to_string(JsonErrorCode code) noexcept;

/**
 * @brief Maps a JsonErrorCode to a broad Aevox error category.
 *
 * @param code  The JSON error code to classify.
 * @return Broad error category for generic handling and logging.
 * @note Thread-safety: safe to call concurrently.
 */
[[nodiscard]] ErrorCategory category(JsonErrorCode code) noexcept;

/**
 * @brief Structured error type for JSON parse and serialization failures.
 *
 * Returned via `std::unexpected` from all `aevox::JsonBackend` operations,
 * `Request::json<T>()`, and `Response::json(const T&)`. Never thrown.
 *
 * @note Thread-safety: value type — safe to copy and move across threads.
 * @note Move semantics: moved-from `JsonError` has `message()` returning an
 *       empty `std::string_view`. The object remains valid and destructible.
 * @note Ownership: stores the message string by value.
 */
class JsonError
{
public:
    /**
     * @brief Constructs a `JsonError` with a stable code and human-readable description.
     *
     * @param code     Stable category of the JSON failure.
     * @param message  Description of the failure (e.g. glaze error string,
     *                 "unexpected end of input", "type mismatch at key 'age'").
     *                 Stored by value — ownership is taken from the argument.
     * @note Thread-safety: constructed value is safe to copy and move across threads.
     */
    explicit JsonError(JsonErrorCode code, std::string message) noexcept;

    /**
     * @brief Constructs a `JsonError` with an unknown code and description.
     *
     * @param message  Description of the failure (e.g. glaze error string,
     *                 "unexpected end of input", "type mismatch at key 'age'").
     *                 Stored by value — ownership is taken from the argument.
     * @note Preserved for source compatibility; `code()` returns `JsonErrorCode::Unknown`.
     */
    explicit JsonError(std::string message) noexcept;

    /**
     * @brief Returns the stable JSON error code.
     *
     * @return `JsonErrorCode` discriminator for branching.
     * @note Thread-safety: safe to call concurrently on immutable instances.
     */
    [[nodiscard]] JsonErrorCode code() const noexcept;

    /**
     * @brief Returns the human-readable error description.
     *
     * @return Non-owning view into the stored message string.
     *         Valid for the lifetime of this `JsonError` object.
     *         Returns an empty view for a moved-from `JsonError`.
     */
    [[nodiscard]] std::string_view message() const noexcept;

private:
    JsonErrorCode code_{JsonErrorCode::Unknown};
    std::string   message_;
};

/**
 * @brief Maps a JsonError detail object to a broad Aevox error category.
 *
 * @param error  The JSON error detail object to classify.
 * @return Broad error category derived from `error.code()`.
 * @note Thread-safety: safe to call concurrently on immutable instances.
 */
[[nodiscard]] ErrorCategory category(const JsonError& error) noexcept;

} // namespace aevox
