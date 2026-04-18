// src/http/response_impl.cpp
//
// INTERNAL — implementation of all Response methods.
//
// Out-of-line definitions for move ctor/assign/dtor (PIMPL pattern requires
// Impl to be complete; Impl is complete here after including response_impl.hpp).
//
// All static factory methods delegate to the private constructor
// Response(int status_code, std::string body, std::string content_type_value).
//
// Design: AEV-005-arch.md §4.2

#include "http/response_impl.hpp"

namespace aevox {

// =============================================================================
// Move ctor / move assign / destructor (out-of-line PIMPL defaults)
// DEVIATION from ADD §3.3: declared non-inline in header, defaulted here.
// Justification: std::unique_ptr<Impl> requires Impl to be complete. Out-of-line
// defaulting is the canonical PIMPL idiom. (Pre-approved deviation.)
// =============================================================================

Response::Response(Response&&) noexcept            = default;
Response& Response::operator=(Response&&) noexcept = default;
Response::~Response()                              = default;

// =============================================================================
// Private constructor — called only by factory methods
// =============================================================================

Response::Response(int status, std::string body_str, std::string ct_value)
    : impl_{std::make_unique<Impl>()}
{
    impl_->status_code = status;
    impl_->body        = std::move(body_str);
    if (!ct_value.empty()) {
        impl_->headers["Content-Type"] = std::move(ct_value);
    }
}

// =============================================================================
// Accessors
// =============================================================================

int Response::status_code() const noexcept
{
    if (!impl_)
        return 0;
    return impl_->status_code;
}

std::string_view Response::body_view() const noexcept
{
    if (!impl_)
        return {};
    return impl_->body;
}

std::optional<std::string_view> Response::get_header(std::string_view name) const noexcept
{
    if (!impl_)
        return std::nullopt;
    auto it = impl_->headers.find(std::string{name});
    if (it == impl_->headers.end())
        return std::nullopt;
    return std::string_view{it->second};
}

// =============================================================================
// Fluent builder — lvalue overloads (modify in place, return reference)
// =============================================================================

Response& Response::content_type(std::string_view ct) &
{
    impl_->headers["Content-Type"] = std::string{ct};
    return *this;
}

Response& Response::header(std::string_view name, std::string_view value) &
{
    impl_->headers[std::string{name}] = std::string{value};
    return *this;
}

// =============================================================================
// Fluent builder — rvalue overloads (consume *this, return by value)
// =============================================================================

Response Response::content_type(std::string_view ct) &&
{
    impl_->headers["Content-Type"] = std::string{ct};
    return std::move(*this);
}

Response Response::header(std::string_view name, std::string_view value) &&
{
    impl_->headers[std::string{name}] = std::string{value};
    return std::move(*this);
}

// =============================================================================
// Static factory methods
// =============================================================================

Response Response::ok(std::string_view body)
{
    return Response{200, std::string{body}, "text/plain"};
}

Response Response::created(std::string_view body)
{
    return Response{201, std::string{body}, "text/plain"};
}

Response Response::not_found(std::string_view body)
{
    return Response{404, std::string{body}, "text/plain"};
}

Response Response::bad_request(std::string_view body)
{
    return Response{400, std::string{body}, "text/plain"};
}

Response Response::unauthorized(std::string_view body)
{
    return Response{401, std::string{body}, "text/plain"};
}

Response Response::forbidden(std::string_view body)
{
    return Response{403, std::string{body}, "text/plain"};
}

Response Response::json(std::string body)
{
    return Response{200, std::move(body), "application/json"};
}

Response Response::stream(std::string_view content_type)
{
    // v0.1: returns a normal Response with empty body and the given Content-Type.
    // The streaming write API is designed in AEV-006. This factory exists now so
    // code that calls stream() compiles. AEV-006 must not change this signature.
    return Response{200, {}, std::string{content_type}};
}

} // namespace aevox
