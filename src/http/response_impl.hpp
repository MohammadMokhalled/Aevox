#pragma once
// src/http/response_impl.hpp
//
// INTERNAL — never included outside src/ or tests/.
//
// Defines Response::Impl (the PIMPL) and the template definition for
// Response::json<T>(T&&). The template definition must be visible to any
// TU that instantiates Response::json<T>() — i.e. internal framework code.
//
// Design: AEV-005-arch.md §4.2, §4.3

#include <aevox/response.hpp>

#include <string>
#include <unordered_map>

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
    /// The framework's write path (AEV-004/AEV-006) serializes these to the wire.
    std::unordered_map<std::string, std::string> headers;
};

// =============================================================================
// Template definition — Response::json<T>(T&&)
// =============================================================================

template <typename T>
    requires aevox::Serializable<T>
[[nodiscard]] Response Response::json(T&& /*value*/)
{
    // v0.1 stub — AEV-009 wires in real glaze serialization via
    // Response::Impl::do_json_serialize(). The sentinel body makes it obvious
    // at runtime that the stub is active. Do not use this output as real JSON.
    return Response{200, R"({"error":"not_implemented"})", "application/json"};
}

} // namespace aevox
