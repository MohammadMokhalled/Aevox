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
// Design: Tasks/architecture/AEV-005-arch.md §4.1, §4.4

#include <aevox/request.hpp>
#include <aevox/websocket.hpp>
#include <aevox/websocket_error.hpp>

#include <algorithm>
#include <any>
#include <cctype>
#include <charconv>
#include <string>
#include <unordered_map>
#include <vector>

#include "http/http_parser.hpp"
#include "net/topic_bus.hpp"
#include "net/websocket_handshake.hpp"
#include "net/websocket_session.hpp"

#if defined(AEVOX_JSON_BACKEND_GLAZE)
    #include "json/glaze_backend.hpp"
#endif

namespace aevox {

// =============================================================================
// Request::Impl
// =============================================================================

struct Request::Impl
{
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

    /// Path parameters injected by the Router via Request::set_params().
    /// Keys are parameter names as declared in the route pattern; values are raw strings.
    std::unordered_map<std::string, std::string> params;

    /// Per-request middleware context bag. Keys are application-defined strings
    /// (e.g. "auth.user"). Values are type-erased via std::any.
    std::unordered_map<std::string, std::any> context;

    // -------------------------------------------------------------------------
    // WebSocket upgrade support — set by the connection handler before dispatch.
    // -------------------------------------------------------------------------

    /// Non-owning pointer to the connection's TcpStream.
    /// Set by the connection handler so that upgrade_websocket() can consume it.
    /// Null for requests that were not set up for WebSocket upgrade.
    aevox::TcpStream* stream{nullptr};

    /// Non-owning pointer to the App-owned TopicBus (null if not a WS route).
    aevox::net::TopicBus* topic_bus{nullptr};

    /// Maximum payload bytes (from AppConfig::max_body_size).
    std::size_t max_payload{10UZ * 1024UZ * 1024UZ}; // 10 MiB default

    /// WebSocket lifecycle callbacks — set by App::ws() dispatch before upgrade_websocket().
    aevox::WebSocketHandler ws_handler;

    /// Constructs Impl, taking ownership of buffer and the parsed request.
    /// Computes path_view and query_view from parsed.target by splitting at '?'.
    Impl(std::vector<std::byte> buf, aevox::detail::ParsedRequest pr,
         std::unordered_map<std::string, std::string> initial_params = {})
        : buffer{std::move(buf)}, parsed{std::move(pr)}, params{std::move(initial_params)}
    {
        // Split parsed.target on the first '?' to compute path and query views.
        // Both path_view and query_view are views into buffer (via parsed.target).
        const auto pos = parsed.target.find('?');
        if (pos == std::string_view::npos) {
            path_view  = parsed.target;
            query_view = {};
        }
        else {
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
[[nodiscard]] std::expected<T, ParamError> Request::param(std::string_view name) const noexcept
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
    }
    else if constexpr (std::same_as<T, std::string>) {
        // Owning copy.
        return raw;
    }
    else {
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
[[nodiscard]] aevox::Task<std::expected<T, aevox::JsonError>> Request::json() const
{
#if defined(AEVOX_JSON_BACKEND_GLAZE)
    // Body bytes are stable for the request lifetime (owned by Impl::buffer).
    // The body span points into the parser's buffer; convert to string_view
    // for the backend. No copy is performed.
    // reinterpret_cast: std::byte* -> const char* — well-defined per
    // [basic.types]/2 as std::byte is an alias for unsigned char.
    const auto body_sv = std::string_view{reinterpret_cast<const char*>(impl_->parsed.body.data()),
                                          impl_->parsed.body.size()};
    constexpr aevox::internal::GlazeBackend kBackend{};
    co_return kBackend.template deserialize<T>(body_sv);
#else
    co_return std::unexpected(aevox::JsonError{"No JSON backend configured. "
                                               "Set AEVOX_JSON_BACKEND to 'glaze' in CMake."});
#endif
}

// =============================================================================
// Template definitions — set<T>() and get<T>()
// =============================================================================

template <typename T> void Request::set(std::string_view key, T&& value)
{
    // Store by value in the context bag. std::string key ensures the key
    // outlives the set() call site (no dangling view risk).
    impl_->context.insert_or_assign(std::string{key}, std::any{std::forward<T>(value)});
}

template <typename T> [[nodiscard]] std::optional<T> Request::get(std::string_view key) const
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
// Header lookup helpers (case-insensitive scan over ParsedRequest::headers)
// =============================================================================

namespace {

/// Case-insensitive equality check for ASCII strings.
[[nodiscard]] inline bool ws_iequal(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

/// Returns true if haystack case-insensitively contains needle.
[[nodiscard]] inline bool ws_icontains(std::string_view haystack, std::string_view needle) noexcept
{
    if (needle.empty())
        return true;
    if (haystack.size() < needle.size())
        return false;
    for (std::size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
        if (ws_iequal(haystack.substr(i, needle.size()), needle))
            return true;
    }
    return false;
}

/// Finds the first header with a case-insensitive name match.
/// Returns the value, or empty string_view if not found.
[[nodiscard]] inline std::string_view ws_find_header(
    const std::vector<std::pair<std::string_view, std::string_view>>& headers,
    std::string_view                                                  name) noexcept
{
    for (const auto& [hname, hval] : headers) {
        if (ws_iequal(hname, name))
            return hval;
    }
    return {};
}

} // anonymous namespace

// =============================================================================
// Request::is_websocket_upgrade()
// =============================================================================

inline bool Request::is_websocket_upgrade() const noexcept
{
    if (!impl_)
        return false;

    const auto& headers = impl_->parsed.headers;

    // Check Upgrade: websocket (case-insensitive value).
    const std::string_view upgrade_val = ws_find_header(headers, "Upgrade");
    if (!ws_iequal(upgrade_val, "websocket"))
        return false;

    // Check Connection: contains "upgrade" (case-insensitive).
    const std::string_view conn_val = ws_find_header(headers, "Connection");
    if (!ws_icontains(conn_val, "upgrade"))
        return false;

    // Check Sec-WebSocket-Key is present and non-empty.
    const std::string_view key_val = ws_find_header(headers, "Sec-WebSocket-Key");
    if (key_val.empty())
        return false;

    return true;
}

// =============================================================================
// Request::upgrade_websocket()
// =============================================================================

inline aevox::Task<std::expected<aevox::WebSocket, aevox::WebSocketError>>
Request::upgrade_websocket()
{
    if (!impl_ || !impl_->stream) {
        co_return std::unexpected(aevox::WebSocketError{aevox::WebSocketErrorCode::InvalidHandshake,
                                                        "No TcpStream available for upgrade"});
    }

    // Build HandshakeHeaders from the parsed request.
    aevox::net::HandshakeHeaders hs_headers;
    {
        const auto& headers          = impl_->parsed.headers;
        hs_headers.upgrade           = ws_find_header(headers, "Upgrade");
        hs_headers.connection        = ws_find_header(headers, "Connection");
        hs_headers.sec_websocket_key = ws_find_header(headers, "Sec-WebSocket-Key");
    }

    // Validate upgrade headers.
    auto validate_result = aevox::net::validate(hs_headers);
    if (!validate_result) {
        co_return std::unexpected(std::move(validate_result.error()));
    }

    // Compute Sec-WebSocket-Accept.
    const std::string accept_key = aevox::net::compute_accept_key(hs_headers.sec_websocket_key);

    // Build HTTP 101 response.
    const std::string response_str = "HTTP/1.1 101 Switching Protocols\r\n"
                                     "Upgrade: websocket\r\n"
                                     "Connection: Upgrade\r\n"
                                     "Sec-WebSocket-Accept: " +
                                     accept_key + "\r\n\r\n";

    // Write the 101 response.
    const std::span<const std::byte> resp_bytes{reinterpret_cast<const std::byte*>(
                                                    response_str.data()),
                                                response_str.size()};
    auto                             write_res = co_await impl_->stream->write(resp_bytes);
    if (!write_res) {
        co_return std::unexpected(aevox::WebSocketError{aevox::WebSocketErrorCode::SendFailed,
                                                        "Failed to write HTTP 101 response"});
    }

    // Extract remote address from the TcpStream (not available directly —
    // store empty string for now; address is retrieved before upgrade in app_impl).
    // The actual remote address is stored in impl_->parsed or passed separately.
    // For v0.2, we use an empty string fallback; app_impl.cpp sets the proper address
    // via a separate mechanism when WebSocket routes are used.
    std::string remote_addr;
    auto        remote_it = impl_->params.find("__remote_addr__");
    if (remote_it != impl_->params.end())
        remote_addr = remote_it->second;

    // Extract the first path parameter as the initial topic.
    std::string initial_topic;
    // The first non-internal parameter is the initial topic.
    for (const auto& [k, v] : impl_->params) {
        if (!k.empty() && k[0] != '_') {
            initial_topic = v;
            break;
        }
    }

    // Create the WebSocketSession (takes ownership of the TcpStream).
    auto session = aevox::net::WebSocketSession::create(
        std::move(*impl_->stream), impl_->topic_bus,
        std::move(impl_->ws_handler), // set by App::ws() dispatch before this call
        impl_->max_payload, std::move(remote_addr), std::move(initial_topic));

    // Null out the stream pointer (it was moved).
    impl_->stream = nullptr;

    // Build and return the WebSocket handle.
    co_return session->make_handle(session);
}

// =============================================================================
// Router — path parameter injection
// =============================================================================
//
// set_params() was removed from the public header (M1 fix: avoids pulling
// <unordered_map> into a public header). The Router injects captured
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
inline Request make_request_from_impl(std::vector<std::byte>       buffer,
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
/// Used by tests (to set params after construction) and the Router
/// (which uses friend class Router to directly write req.impl_->params).
/// Returns nullptr for a moved-from Request.
/// Friend of Request — declared in request.hpp (in aevox namespace, not detail).
inline Request::Impl* get_mutable_request_impl(Request& req) noexcept
{
    return req.impl_.get();
}

} // namespace aevox
