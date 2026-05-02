#pragma once
// src/json/glaze_backend.hpp
//
// INTERNAL — never included outside src/ or tests/.
//
// Default JSON backend backed by glaze (header-only).
// This is the ONLY file in the codebase permitted to include <glaze/glaze.hpp>.
// All glaze types are fully confined to this translation unit.

#include <aevox/json_backend.hpp>
#include <aevox/json_error.hpp>

#include <glaze/glaze.hpp>

#include <expected>
#include <string>
#include <string_view>

namespace aevox::internal {

/**
 * @brief glaze-backed JSON backend satisfying `aevox::JsonBackend`.
 *
 * Wraps `glz::read_json` and `glz::write_json` from glaze. All deserialization
 * and serialization errors are mapped to `aevox::JsonError` — no glaze types
 * escape this struct.
 *
 * @note Thread-safety: stateless. All calls operate on local variables.
 *       Safe to call concurrently from any number of threads.
 * @note Move semantics: trivially movable (no data members).
 * @note glaze is header-only; all template instantiations occur in the
 *       translation unit that includes this header. Compile time is paid
 *       once per TU per distinct T instantiated.
 */
struct GlazeBackend
{
    /**
     * @brief Deserializes JSON bytes into type `T`.
     *
     * @tparam T  Target type. Must be glaze-reflectable (aggregate struct
     *            with public fields, or explicitly annotated with
     *            `glz::meta`). Compilation fails for non-reflectable T.
     * @param  input  JSON text as a string view.
     * @return        Populated `T` on success.
     *                `aevox::JsonError` with a formatted message on failure.
     *                Error cases: truncated input, invalid UTF-8, type
     *                mismatch, missing required field.
     */
    template <typename T>
    [[nodiscard]] std::expected<T, aevox::JsonError> deserialize(std::string_view input) const
    {
        T result{};
        // glz::read_json returns glz::error_ctx; truthy when an error occurred.
        // glz::format_error formats the error relative to the input string for
        // position-annotated diagnostic messages.
        const auto ec = glz::read_json(result, input);
        if (ec) {
            return std::unexpected(aevox::JsonError{glz::format_error(ec, input)});
        }
        return result;
    }

    /**
     * @brief Serializes `value` to a JSON string.
     *
     * @tparam T  Type to serialize. Must be glaze-reflectable.
     * @param  value  Const reference to the value to serialize.
     * @return        JSON string on success.
     *                `aevox::JsonError` with a formatted message on failure.
     */
    template <typename T>
    [[nodiscard]] std::expected<std::string, aevox::JsonError> serialize(const T& value) const
    {
        // glz::write_json (single-arg) returns std::expected<std::string, error_ctx>.
        auto out = glz::write_json(value);
        if (!out) {
            return std::unexpected(aevox::JsonError{glz::format_error(out.error())});
        }
        return std::move(*out);
    }
};

// Compile-time verification that GlazeBackend satisfies the concept.
// This fires in every TU that includes this header, providing an early
// and precise error if the glaze API changes between versions.
static_assert(aevox::JsonBackend<GlazeBackend>, "GlazeBackend must satisfy aevox::JsonBackend");

} // namespace aevox::internal
