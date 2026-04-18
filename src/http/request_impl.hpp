#pragma once
// src/http/request_impl.hpp
//
// INTERNAL — never included outside src/ or tests/.
//
// Defines Request::Impl (the PIMPL) and provides full template definitions
// for Request::param<T>(), Request::json<T>(), Request::set<T>(), and
// Request::get<T>(). These definitions must be visible to any TU that
// instantiates the templates (i.e. internal framework code and tests).
//
// Buffer lifetime contract:
//   Impl owns both the raw TCP read buffer (buffer) and the ParsedRequest
//   derived from it. std::string_view fields in parsed.method, parsed.target,
//   and parsed.headers point into buffer. Moving std::vector<std::byte> does
//   NOT invalidate existing pointers/references into it — the move transfers
//   ownership of the heap allocation, preserving all addresses.
//   parsed.body is a span into the parser's internal chunk_buf (owned by the
//   ConnectionHandler, not by Impl). It must not be used after the parser is
//   reset or destroyed.
//
// Design: AEV-005-arch.md §4.1, §4.4

#include <aevox/request.hpp>

#include "http/http_parser.hpp"

#include <algorithm>
#include <any>
#include <cctype>
#include <charconv>
#include <string>
#include <unordered_map>
#include <vector>

namespace aevox {

// =============================================================================
// Request::Impl
// =============================================================================

struct Request::Impl {
    /// Raw TCP read buffer — owns the memory that parsed string_views point into.
    std::vector<std::byte> buffer;

    /// Structured view of the parsed request. method, target, and headers are
    /// zero-copy views into buffer. body is a span into the parser's chunk_buf
    /// (owned by ConnectionHandler, not by this Impl).
    aevox::detail::ParsedRequest parsed;

    /// Cached split of parsed.target at the first '?'.
    /// path_view is the portion before '?'; query_view is the portion after.
    /// Both are zero-copy views into buffer (since target is a view into buffer).
    std::string_view path_view;
    std::string_view query_view;

    /// Path parameters injected by the Router (AEV-004) via Request::set_params().
    /// Keys are parameter names as declared in the route pattern; values are raw strings.
    std::unordered_map<std::string, std::string> params;

    /// Per-request middleware context bag. Keys are application-defined strings
    /// (e.g. "auth.user"). Values are type-erased via std::any.
    std::unordered_map<std::string, std::any> context;

    /// Constructs Impl, taking ownership of buffer and the parsed request.
    /// Computes path_view and query_view from parsed.target by splitting at '?'.
    Impl(std::vector<std::byte> buf,
         aevox::detail::ParsedRequest pr,
         std::unordered_map<std::string, std::string> initial_params = {})
        : buffer{std::move(buf)}
        , parsed{std::move(pr)}
        , params{std::move(initial_params)}
    {
        // Split parsed.target on the first '?' to compute path and query views.
        // Both path_view and query_view are views into buffer (via parsed.target).
        const auto pos = parsed.target.find('?');
        if (pos == std::string_view::npos) {
            path_view  = parsed.target;
            query_view = {};
        } else {
            path_view  = parsed.target.substr(0, pos);
            query_view = parsed.target.substr(pos + 1);
        }
    }
};

// =============================================================================
// Template definitions — param<T>()
// =============================================================================

template <typename T>
    requires aevox::ParamConvertible<T>
[[nodiscard]] std::expected<T, ParamError>
Request::param(std::string_view name) const noexcept
{
    // Look up the parameter by name in the router-injected params map.
    auto it = impl_->params.find(std::string{name});
    if (it == impl_->params.end()) {
        return std::unexpected(ParamError::NotFound);
    }

    const std::string& raw = it->second;

    if constexpr (std::same_as<T, std::string_view>) {
        // Zero-copy: return a view into the params map entry.
        // Lifetime: tied to this Request (params map is owned by Impl).
        return std::string_view{raw};
    } else if constexpr (std::same_as<T, std::string>) {
        // Owning copy.
        return raw;
    } else {
        // Arithmetic type (integral or floating_point) — use from_chars.
        T result{};
        const auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), result);
        if (ec != std::errc{} || ptr != raw.data() + raw.size()) {
            return std::unexpected(ParamError::BadConversion);
        }
        return result;
    }
}

// =============================================================================
// Template definitions — json<T>()
// =============================================================================

template <typename T>
    requires aevox::Deserializable<T>
[[nodiscard]] aevox::Task<std::expected<T, BodyParseError>>
Request::json() const
{
    // v0.1 stub — real implementation wired by AEV-009 via Impl::do_json_parse().
    co_return std::unexpected(BodyParseError::NotImplemented);
}

// =============================================================================
// Template definitions — set<T>() and get<T>()
// =============================================================================

template <typename T>
void Request::set(std::string_view key, T&& value)
{
    // Store by value in the context bag. std::string key ensures the key
    // outlives the set() call site (no dangling view risk).
    impl_->context.insert_or_assign(std::string{key}, std::any{std::forward<T>(value)});
}

template <typename T>
[[nodiscard]] std::optional<T> Request::get(std::string_view key) const
{
    auto it = impl_->context.find(std::string{key});
    if (it == impl_->context.end()) {
        return std::nullopt;
    }
    // std::any_cast returns nullptr on type mismatch when used with pointer form.
    const T* ptr = std::any_cast<T>(&it->second);
    if (ptr == nullptr) {
        return std::nullopt;
    }
    return *ptr;
}

// =============================================================================
// AEV-004 Router — path parameter injection
// =============================================================================
//
// set_params() was removed from the public header (M1 fix: avoids pulling
// <unordered_map> into a public header). AEV-004 (Router) injects captured
// path parameters directly through its friend-class access to Request::Impl:
//
//   req.impl_->params = std::move(captured_params);
//
// This is valid because `friend class Router` (declared in request.hpp) grants
// the Router class access to all private members of Request, including impl_.
// Since the Router includes this header, Impl is complete at that point.

// =============================================================================
// Internal factory helpers (friend of Request — see request.hpp)
// =============================================================================

/// Creates a Request from a pre-built Impl. Used by ConnectionHandler and tests.
/// Application code cannot call this because Impl is incomplete outside src/.
/// Friend of Request — declared in request.hpp (in aevox namespace, not detail).
inline Request make_request_from_impl(std::unique_ptr<Request::Impl> impl) noexcept
{
    return Request{std::move(impl)};
}

/// Creates a Request from a raw byte buffer and a parsed request.
/// Path parameters are NOT set here — call get_mutable_request_impl() after
/// construction to inject params via impl->params = std::move(params).
/// Used by tests and ConnectionHandler. Friend of Request.
/// Not noexcept because std::make_unique<Impl> may throw std::bad_alloc.
inline Request make_request_from_impl(
    std::vector<std::byte>       buffer,
    aevox::detail::ParsedRequest parsed)
{
    return make_request_from_impl(
        std::make_unique<Request::Impl>(std::move(buffer), std::move(parsed)));
}

/// Returns a read-only pointer to Request's Impl for internal inspection (tests).
/// Returns nullptr for a moved-from Request.
/// Friend of Request — declared in request.hpp (in aevox namespace, not detail).
inline const Request::Impl* get_request_impl(const Request& req) noexcept
{
    return req.impl_.get();
}

/// Returns a mutable pointer to Request's Impl for internal param injection.
/// Used by tests (to set params after construction) and AEV-004 Router
/// (which uses friend class Router to directly write req.impl_->params).
/// Returns nullptr for a moved-from Request.
/// Friend of Request — declared in request.hpp (in aevox namespace, not detail).
inline Request::Impl* get_mutable_request_impl(Request& req) noexcept
{
    return req.impl_.get();
}

} // namespace aevox
