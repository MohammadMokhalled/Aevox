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
#include <string>
#include <unordered_map>

#if defined(AEVOX_JSON_BACKEND_GLAZE)
    #include "json/glaze_backend.hpp"
#endif

namespace aevox {

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
        return Response{500, error_body, "application/json"};
    }
    return Response{200, std::move(*result), "application/json"};
#else
    (void)value;
    return Response{
        500, R"({"error":"no_json_backend","detail":"Set AEVOX_JSON_BACKEND to glaze in CMake."})",
        "application/json"};
#endif
}

// =============================================================================
// Internal serialization accessor (friend of Response — see response.hpp)
// =============================================================================

/// Returns a read-only pointer to Response's Impl for serialization.
/// Used by src/router/app_impl.cpp to iterate all headers when writing
/// HTTP responses to the wire. Returns nullptr for a moved-from Response.
/// Friend of Response — declared in response.hpp (in aevox namespace, not detail).
inline const Response::Impl* get_response_impl(const Response& res) noexcept
{
    return res.impl_.get();
}

} // namespace aevox
