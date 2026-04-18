// src/http/request_impl.cpp
//
// INTERNAL — implementation of all non-template Request methods.
//
// Out-of-line definitions for move ctor/assign/dtor (PIMPL pattern requires
// Impl to be complete; Impl is complete here after including request_impl.hpp).
//
// Also provides explicit instantiations for all five ParamConvertible types so
// that code in src/ can call Request::param<T>() without including the template
// definition header. AEV-004 (Router) must include request_impl.hpp directly
// if it requires additional instantiations.
//
// Design: AEV-005-arch.md §4.1

#include "http/request_impl.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <string_view>

namespace aevox {

// =============================================================================
// Move ctor / move assign / destructor (out-of-line PIMPL defaults)
// DEVIATION from ADD §3.2: declared non-inline in header, defaulted here.
// Justification: std::unique_ptr<Impl> requires Impl to be complete at the
// point where the defaulted special members are instantiated. Impl is only
// complete after request_impl.hpp is included, which the public header never
// does. Out-of-line defaulting is the canonical PIMPL idiom.
// =============================================================================

Request::Request(Request&&) noexcept            = default;
Request& Request::operator=(Request&&) noexcept = default;
Request::~Request()                             = default;

// =============================================================================
// Private constructor — called only by detail::ConnectionHandler
// =============================================================================

Request::Request(std::unique_ptr<Impl> impl) noexcept
    : impl_{std::move(impl)}
{}

// =============================================================================
// valid()
// =============================================================================

bool Request::valid() const noexcept
{
    return impl_ != nullptr;
}

// =============================================================================
// method()
// =============================================================================

HttpMethod Request::method() const noexcept
{
    // HTTP methods are case-sensitive uppercase per RFC 7230. Map the seven
    // supported verbs; anything else yields HttpMethod::Unknown.
    const std::string_view m = impl_->parsed.method;
    if (m == "GET")     return HttpMethod::GET;
    if (m == "POST")    return HttpMethod::POST;
    if (m == "PUT")     return HttpMethod::PUT;
    if (m == "PATCH")   return HttpMethod::PATCH;
    if (m == "DELETE")  return HttpMethod::DELETE;
    if (m == "HEAD")    return HttpMethod::HEAD;
    if (m == "OPTIONS") return HttpMethod::OPTIONS;
    return HttpMethod::Unknown;
}

// =============================================================================
// path() and query()
// =============================================================================

std::string_view Request::path() const noexcept
{
    return impl_->path_view;
}

std::string_view Request::query() const noexcept
{
    return impl_->query_view;
}

// =============================================================================
// header() — case-insensitive lookup, RFC 7230 §3.2
// =============================================================================

std::optional<std::string_view>
Request::header(std::string_view name) const noexcept
{
    // Case-insensitive comparison: lowercase both characters before comparing.
    // Uses std::ranges::find_if over the parsed headers vector (linear scan;
    // typical requests have < 30 headers — no heap allocation in this path).
    auto it = std::ranges::find_if(
        impl_->parsed.headers,
        [name](const auto& pair) {
            return std::ranges::equal(
                pair.first, name,
                [](char a, char b) noexcept {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                });
        });

    if (it == impl_->parsed.headers.end()) {
        return std::nullopt;
    }
    return it->second;
}

// =============================================================================
// body()
// =============================================================================

std::span<const std::byte> Request::body() const noexcept
{
    return impl_->parsed.body;
}

// =============================================================================
// set_params() — called by Router (AEV-004) before handler dispatch
// =============================================================================

void Request::set_params(std::unordered_map<std::string, std::string> params) noexcept
{
    impl_->params = std::move(params);
}

// =============================================================================
// to_string(HttpMethod)
// =============================================================================

std::string_view to_string(HttpMethod m) noexcept
{
    switch (m) {
    case HttpMethod::GET:     return "GET";
    case HttpMethod::POST:    return "POST";
    case HttpMethod::PUT:     return "PUT";
    case HttpMethod::PATCH:   return "PATCH";
    case HttpMethod::DELETE:  return "DELETE";
    case HttpMethod::HEAD:    return "HEAD";
    case HttpMethod::OPTIONS: return "OPTIONS";
    case HttpMethod::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN"; // unreachable; silences -Wreturn-type
}

// =============================================================================
// Explicit instantiations — ParamConvertible types
//
// These instantiations allow TUs that only link against aevox_core (without
// including request_impl.hpp) to call param<T>() for these five types.
// AEV-004 (Router) must add any additional T types it uses to this list.
// A missing instantiation will manifest as a linker error — not silent UB.
// =============================================================================

template std::expected<int, ParamError>
    Request::param<int>(std::string_view) const noexcept;

template std::expected<long, ParamError>
    Request::param<long>(std::string_view) const noexcept;

template std::expected<double, ParamError>
    Request::param<double>(std::string_view) const noexcept;

template std::expected<std::string, ParamError>
    Request::param<std::string>(std::string_view) const noexcept;

template std::expected<std::string_view, ParamError>
    Request::param<std::string_view>(std::string_view) const noexcept;

} // namespace aevox
