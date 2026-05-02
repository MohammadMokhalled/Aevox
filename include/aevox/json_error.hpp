#pragma once
// include/aevox/json_error.hpp
//
// Structured error type for all JSON operations.
// No third-party library types. Standard C++ only.

#include <string>
#include <string_view>

namespace aevox {

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
     * @brief Constructs a `JsonError` with a human-readable description.
     *
     * @param message  Description of the failure (e.g. glaze error string,
     *                 "unexpected end of input", "type mismatch at key 'age'").
     *                 Stored by value — ownership is taken from the argument.
     */
    explicit JsonError(std::string message) noexcept : message_{std::move(message)} {}

    /**
     * @brief Returns the human-readable error description.
     *
     * @return Non-owning view into the stored message string.
     *         Valid for the lifetime of this `JsonError` object.
     *         Returns an empty view for a moved-from `JsonError`.
     */
    [[nodiscard]] std::string_view message() const noexcept;

private:
    std::string message_;
};

} // namespace aevox
