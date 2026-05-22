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
//   Impl owns the raw TCP read buffer (buffer) and the ParsedRequest derived
//   from it. ParsedRequest method, target, and headers are parser-owned views
//   valid until the connection parser is reset after request handling.
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
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "http/http_parser.hpp"
#include "log/request_context.hpp"
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

class Request::Impl
{
private:
    /// Raw TCP read buffer owned by the Request for body/transport lifetime.
    std::vector<std::byte> buffer_;

    /// Structured view of the parsed request. method, target, and headers are
    /// parser-owned views valid until the connection parser is reset. body is a
    /// span into the parser's chunk_buf (owned by ConnectionHandler, not by this Impl).
    aevox::detail::ParsedRequest parsed_;

    /// Cached split of parsed.target at the first '?'.
    /// path_view is the portion before '?'; query_view is the portion after.
    /// Both views refer to parser-owned target storage and share its lifetime.
    std::string_view path_view_;
    std::string_view query_view_;

    /// Path parameters injected by the Router via Request::set_params().
    /// Keys are parameter names as declared in the route pattern; values are raw strings.
    std::unordered_map<std::string, std::string> params_;

    /// Per-request middleware context bag. Keys are application-defined strings
    /// (e.g. "auth.user"). Values are type-erased via std::any.
    std::unordered_map<std::string, std::any> context_;

    // -------------------------------------------------------------------------
    // WebSocket upgrade support — set by the connection handler before dispatch.
    // -------------------------------------------------------------------------

    /// Non-owning reference to the connection's TcpStream.
    /// Set by the connection handler so that upgrade_websocket() can consume it.
    /// Empty for requests that were not set up for WebSocket upgrade.
    std::optional<std::reference_wrapper<aevox::TcpStream>> stream_;

    /// Non-owning reference to the App-owned TopicBus, empty if not a WS route.
    std::optional<std::reference_wrapper<aevox::net::TopicBus>> topic_bus_;

    /// Maximum payload bytes (from AppConfig::max_body_size).
    static constexpr std::size_t kDefaultMaxPayloadBytes{10UZ * 1024UZ * 1024UZ};
    std::size_t                  max_payload_{kDefaultMaxPayloadBytes};

    /// WebSocket lifecycle callbacks — set by App::ws() dispatch before upgrade_websocket().
    aevox::WebSocketHandler ws_handler_;

    /// Per-request logging context.
    RequestContext log_context_;

public:
    /// Constructs Impl, taking ownership of buffer and the parsed request.
    /// Computes path_view and query_view from parsed.target by splitting at '?'.
    Impl(std::vector<std::byte> buf, aevox::detail::ParsedRequest pr,
         std::unordered_map<std::string, std::string> initial_params = {})
        : buffer_{std::move(buf)}, parsed_{std::move(pr)}, params_{std::move(initial_params)}
    {
        // Split parsed.target on the first '?' to compute path and query views.
        // Both path_view and query_view are views into buffer (via parsed.target).
        const auto pos = parsed_.target.find('?');
        if (pos == std::string_view::npos) {
            path_view_  = parsed_.target;
            query_view_ = {};
        }
        else {
            path_view_  = parsed_.target.substr(0, pos);
            query_view_ = parsed_.target.substr(pos + 1);
        }
    }

    [[nodiscard]] const aevox::detail::ParsedRequest& parsed() const noexcept
    {
        return parsed_;
    }
    [[nodiscard]] std::string_view path_view() const noexcept
    {
        return path_view_;
    }
    [[nodiscard]] std::string_view query_view() const noexcept
    {
        return query_view_;
    }
    [[nodiscard]] std::unordered_map<std::string, std::string>& params() noexcept
    {
        return params_;
    }
    [[nodiscard]] const std::unordered_map<std::string, std::string>& params() const noexcept
    {
        return params_;
    }
    [[nodiscard]] std::unordered_map<std::string, std::any>& context() noexcept
    {
        return context_;
    }
    [[nodiscard]] std::optional<std::reference_wrapper<aevox::TcpStream>> stream() const noexcept
    {
        return stream_;
    }
    void set_stream(aevox::TcpStream& stream) noexcept
    {
        stream_ = stream;
    }
    void clear_stream() noexcept
    {
        stream_.reset();
    }
    [[nodiscard]] std::optional<std::reference_wrapper<aevox::net::TopicBus>> topic_bus()
        const noexcept
    {
        return topic_bus_;
    }
    void set_topic_bus(aevox::net::TopicBus& topic_bus) noexcept
    {
        topic_bus_ = topic_bus;
    }
    [[nodiscard]] std::size_t max_payload() const noexcept
    {
        return max_payload_;
    }
    void set_max_payload(std::size_t max_payload) noexcept
    {
        max_payload_ = max_payload;
    }
    [[nodiscard]] aevox::WebSocketHandler& ws_handler() noexcept
    {
        return ws_handler_;
    }
    [[nodiscard]] RequestContext& log_context() noexcept
    {
        return log_context_;
    }
    [[nodiscard]] const RequestContext& log_context() const noexcept
    {
        return log_context_;
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
    auto it = impl_->params().find(std::string{name});
    if (it == impl_->params().end()) {
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
        T                 result{};
        const char* const first      = raw.c_str();
        const auto        char_count = static_cast<std::ptrdiff_t>(raw.size());
        const char* const last       = std::next(first, char_count);
        const auto [ptr, ec]         = std::from_chars(first, last, result);
        if (ec != std::errc{} || ptr != last) {
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
    std::string body_text;
    body_text.reserve(impl_->parsed().body.size());
    for (const std::byte byte : impl_->parsed().body) {
        body_text.push_back(std::to_integer<char>(byte));
    }
    const auto                              body_sv = std::string_view{body_text};
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
    impl_->context().insert_or_assign(std::string{key}, std::any{std::forward<T>(value)});
}

template <typename T> [[nodiscard]] std::optional<T> Request::get(std::string_view key) const
{
    auto it = impl_->context().find(std::string{key});
    if (it == impl_->context().end()) {
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

    const auto& headers = impl_->parsed().headers;

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
    if (!impl_) {
        co_return std::unexpected(aevox::WebSocketError{aevox::WebSocketErrorCode::InvalidHandshake,
                                                        "No TcpStream available for upgrade"});
    }

    auto stream_ref = impl_->stream();
    if (!stream_ref) {
        co_return std::unexpected(aevox::WebSocketError{aevox::WebSocketErrorCode::InvalidHandshake,
                                                        "No TcpStream available for upgrade"});
    }

    auto& stream        = stream_ref.value().get();
    auto  topic_bus_ref = impl_->topic_bus();

    // Build HandshakeHeaders from the parsed request.
    aevox::net::HandshakeHeaders hs_headers;
    {
        const auto& headers          = impl_->parsed().headers;
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
    const auto response_chars = std::span<const char>{response_str.data(), response_str.size()};
    const auto resp_bytes     = std::as_bytes(response_chars);
    auto       write_res      = co_await stream.write(resp_bytes);
    if (!write_res) {
        co_return std::unexpected(aevox::WebSocketError{aevox::WebSocketErrorCode::SendFailed,
                                                        "Failed to write HTTP 101 response"});
    }

    // Extract remote address from the TcpStream (not available directly —
    // store empty string for now; address is retrieved before upgrade in app_impl).
    // The actual remote address is stored in impl_->parsed() or passed separately.
    // For v0.2, we use an empty string fallback; app_impl.cpp sets the proper address
    // via a separate mechanism when WebSocket routes are used.
    std::string remote_addr;
    auto        remote_it = impl_->params().find("__remote_addr__");
    if (remote_it != impl_->params().end())
        remote_addr = remote_it->second;

    // Extract the first path parameter as the initial topic.
    std::string initial_topic;
    // The first non-internal parameter is the initial topic.
    for (const auto& [k, v] : impl_->params()) {
        if (!k.empty() && k[0] != '_') {
            initial_topic = v;
            break;
        }
    }

    // Create the WebSocketSession (takes ownership of the TcpStream).
    auto session = aevox::net::WebSocketSession::create(
        std::move(stream), topic_bus_ref,
        std::move(impl_->ws_handler()), // set by App::ws() dispatch before this call
        impl_->max_payload(), std::move(remote_addr), std::move(initial_topic));

    // Clear the stream reference (it was moved).
    impl_->clear_stream();

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
//   req.impl_->params() = std::move(captured_params);
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

/// Returns a read-only reference to Request's Impl for internal inspection (tests).
/// Returns std::nullopt for a moved-from Request.
/// Friend of Request — declared in request.hpp (in aevox namespace, not detail).
inline std::optional<std::reference_wrapper<const Request::Impl>> get_request_impl(
    const Request& req) noexcept
{
    if (!req.impl_) {
        return std::nullopt;
    }
    return std::cref(*req.impl_);
}

/// Returns a mutable reference to Request's Impl for internal param injection.
/// Used by tests (to set params after construction) and the Router
/// (which uses friend class Router to directly write req.impl_->params()).
/// Returns std::nullopt for a moved-from Request.
/// Friend of Request — declared in request.hpp (in aevox namespace, not detail).
inline std::optional<std::reference_wrapper<Request::Impl>> get_mutable_request_impl(
    Request& req) noexcept
{
    if (!req.impl_) {
        return std::nullopt;
    }
    return std::ref(*req.impl_);
}

} // namespace aevox
