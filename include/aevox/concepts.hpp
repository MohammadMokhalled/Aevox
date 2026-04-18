#pragma once
// include/aevox/concepts.hpp
//
// Framework-wide C++23 concept definitions.
//
// Serializable and Deserializable are placeholder stubs in v0.1.
// AEV-009 (glaze JSON backend) replaces them with real constraints.
// Do not add any includes beyond <concepts>, <string>, and <string_view> here —
// this header is included by request.hpp and response.hpp.
//
// Design: AEV-005-arch.md §3.1

#include <concepts>
#include <string>
#include <string_view>

namespace aevox {

/**
 * @brief Types that can be used as typed path or query parameters.
 *
 * Constrains `Request::param<T>()`. The implementation uses `std::from_chars`
 * for arithmetic types and zero-copy passthrough for string types.
 *
 * Supported: all `std::integral` types, all `std::floating_point` types,
 * `std::string_view` (zero-copy, tied to connection buffer lifetime),
 * and `std::string` (owned copy).
 */
template <typename T>
concept ParamConvertible = std::integral<T> || std::floating_point<T> ||
                           std::same_as<T, std::string_view> || std::same_as<T, std::string>;

/**
 * @brief Placeholder concept: any type is serializable to JSON in v0.1.
 *
 * AEV-009 replaces this with a real glaze-backed constraint. Until then,
 * `Response::json<T>()` will fail at runtime with `SerializeError::NotImplemented`
 * for all T except `std::string` (which has a non-template overload).
 *
 * @note Do not write production code that depends on this being unconstrained.
 *       The v0.1 stub exists only to allow `Response::json<T>()` to compile.
 */
template <typename T>
concept Serializable = true; // replaced in AEV-009

/**
 * @brief Placeholder concept: any type is deserializable from JSON in v0.1.
 *
 * AEV-009 replaces this with a real glaze-backed constraint. Until then,
 * `Request::json<T>()` always returns `BodyParseError::NotImplemented`.
 *
 * @note Do not write production code that depends on this being unconstrained.
 */
template <typename T>
concept Deserializable = true; // replaced in AEV-009

} // namespace aevox
