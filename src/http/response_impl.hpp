#pragma once
// src/http/response_impl.hpp
//
// INTERNAL — never included outside src/ or tests/.
//
// Defines Response::Impl (the PIMPL) and the template definition for
// Response::json<T>(T&&). The template definition must be visible to any
// TU that instantiates Response::json<T>() — i.e. internal framework code.
//
// Design: Tasks/architecture/AEV-005-arch.md §4.2, §4.3

#include <aevox/response.hpp>

#include <format>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#if defined(AEVOX_JSON_BACKEND_GLAZE)
    #include "json/glaze_backend.hpp"
#endif

namespace aevox {

inline constexpr int kStatusOk{200};
inline constexpr int kStatusSwitchingProtocols{101};
inline constexpr int kStatusCreated{201};
inline constexpr int kStatusBadRequest{400};
inline constexpr int kStatusUnauthorized{401};
inline constexpr int kStatusForbidden{403};
inline constexpr int kStatusNotFound{404};
inline constexpr int kStatusMethodNotAllowed{405};
inline constexpr int kStatusInternalServerError{500};

// =============================================================================
// Response::Impl
// =============================================================================

struct Response::Impl
{
    /// HTTP status code (e.g. 200, 404). 0 for a moved-from Response.
    int status_code{0};

    /// Owned response body string.
    std::string body;

    /// Header map: name → value. Names stored as provided (no normalization).
    /// The framework's write path serializes these to the wire.
    std::unordered_map<std::string, std::string> headers;
};

// =============================================================================
// Template definition — Response::json<T>(T&&)
// =============================================================================

template <typename T>
    requires aevox::Serializable<T>
[[nodiscard]] Response Response::json(const T& value)
{
#if defined(AEVOX_JSON_BACKEND_GLAZE)
    constexpr aevox::internal::GlazeBackend kBackend{};
    auto                                    result = kBackend.serialize(value);
    if (!result) {
        const auto detail = std::string{result.error().message()};
        const auto error_body =
            std::format(R"({{"error":"json_serialization_failed","detail":"{}"}})", detail);
        return Response{kStatusInternalServerError, error_body, "application/json"};
    }
    return Response{kStatusOk, std::move(*result), "application/json"};
#else
    (void)value;
    return Response{
        kStatusInternalServerError,
        R"({"error":"no_json_backend","detail":"Set AEVOX_JSON_BACKEND to glaze in CMake."})",
        "application/json"};
#endif
}

// =============================================================================
// Internal serialization accessor (friend of Response — see response.hpp)
// =============================================================================

/// Returns a read-only reference to Response's Impl for serialization.
/// Used by src/router/app_impl.cpp to iterate all headers when writing
/// HTTP responses to the wire. Returns std::nullopt for a moved-from Response.
/// Friend of Response — declared in response.hpp (in aevox namespace, not detail).
inline std::optional<std::reference_wrapper<const Response::Impl>> get_response_impl(
    const Response& res) noexcept
{
    if (!res.impl_) {
        return std::nullopt;
    }
    return std::cref(*res.impl_);
}

} // namespace aevox
