#pragma once
// include/aevox/json_backend.hpp
//
// Concept defining the compile-time contract for JSON backend implementations.
// Standard C++ only. No glaze, no Asio, no third-party includes.

#include <aevox/json_error.hpp>

#include <concepts>
#include <expected>
#include <string>
#include <string_view>

namespace aevox {

namespace detail {

/**
 * @brief Probe type used exclusively by the `JsonBackend` concept.
 *
 * A plain aggregate with two public fields. glaze reflects any struct with
 * public fields automatically (no annotation required), so this type works as
 * a concrete `T` in the requires-expression without requiring any glaze
 * dependency in this header. Non-glaze backends that cannot handle this probe
 * will fail the concept, which is the correct outcome — the constraint fires
 * at the backend level, not at the call site.
 *
 * @note Internal only. Do not use outside of concept definitions.
 */
struct JsonConceptProbe
{
    int   x{};
    float y{};
};

} // namespace detail

/**
 * @brief Concept constraining a JSON backend implementation.
 *
 * A type `B` satisfies `JsonBackend` if and only if it provides:
 * - `deserialize<T>(std::string_view)` returning
 *   `std::expected<T, aevox::JsonError>` for the probe type `T`.
 * - `serialize(const T&)` returning
 *   `std::expected<std::string, aevox::JsonError>` for the probe type `T`.
 *
 * The concept is verified at compile time via a concrete probe type
 * (`aevox::detail::JsonConceptProbe`). Any backend that handles the probe type
 * satisfies the concept. Non-reflectable types that fail individual
 * instantiations are caught at the backend level when `deserialize<T>` or
 * `serialize(const T&)` is instantiated.
 *
 * @tparam B  The backend type to constrain.
 *
 * @note Thread-safety contract: all conforming backends must be thread-safe
 *       for concurrent calls on separate inputs. Backends must carry no
 *       mutable shared state. `serialize` and `deserialize` must be
 *       callable on `const B&`.
 * @note Move semantics: backends are constructed once and used as const
 *       objects for the duration of the application. Move semantics are not
 *       required beyond default construction.
 */
template <typename B>
concept JsonBackend =
    requires(const B& backend, std::string_view input, const detail::JsonConceptProbe& probe) {
        {
            backend.template deserialize<detail::JsonConceptProbe>(input)
        } -> std::same_as<std::expected<detail::JsonConceptProbe, JsonError>>;
        { backend.serialize(probe) } -> std::same_as<std::expected<std::string, JsonError>>;
    };

} // namespace aevox
