#pragma once
// include/aevox/request.hpp
//
// Public aevox::Request class — the primary read interface for HTTP handlers.
//
// Ownership: Request owns the raw read buffer (std::vector<std::byte>) and the
// ParsedRequest derived from it. std::string_view fields returned by method(),
// path(), query(), and header() are valid for the lifetime of the Request object.
//
// Thread-safety: Not thread-safe. Request must be used on the same strand
// as the connection coroutine that owns it. Never share a Request between threads.
//
// Move semantics: Move-only. A moved-from Request has valid() == false.
// Calling any accessor on a moved-from Request is undefined behaviour.
//
// Construction: Only aevox::detail::ConnectionHandler may construct a Request.
//
// Design: Tasks/architecture/AEV-005-arch.md §3.2

#include <aevox/concepts.hpp>
#include <aevox/json_error.hpp>
#include <aevox/task.hpp>
#include <aevox/websocket_error.hpp>

#include <any>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aevox {

// Forward declaration — full definition in include/aevox/websocket.hpp.
// Declared here so that upgrade_websocket() can return WebSocket by value
// without including the full header (which avoids circular dependency issues).
class WebSocket;

// =============================================================================
// HttpMethod
// =============================================================================

/**
 * @brief Typed enumeration of HTTP/1.1 request methods supported by Aevox v0.1.
 *
 * Returned by `Request::method()`. The parser maps the raw method string to
 * this enum — unknown verbs yield `HttpMethod::Unknown`.
 */
// <winnt.h> (pulled in by Asio on Windows) defines DELETE as a numeric macro.
// Push and suppress it while the enumerator is declared, then restore.
#ifdef DELETE
    #pragma push_macro("DELETE")
    #undef DELETE
    #define AEVOX_WIN32_DELETE_PUSHED
#endif

enum class HttpMethod : std::uint8_t
{
    GET,
    POST,
    PUT,
    PATCH,
    DELETE,
    HEAD,
    OPTIONS,
    Unknown, ///< Any verb not listed above (treated as a client error by the router).
};

#ifdef AEVOX_WIN32_DELETE_PUSHED
    #pragma pop_macro("DELETE")
    #undef AEVOX_WIN32_DELETE_PUSHED
#endif

/**
 * @brief Returns the canonical string representation of an HttpMethod.
 *
 * @param m  The method value.
 * @return   A static string literal, e.g. `"GET"`, `"POST"`.
 *           Returns `"UNKNOWN"` for `HttpMethod::Unknown`.
 * @note     noexcept — never allocates, returns a static literal.
 */
[[nodiscard]] std::string_view to_string(HttpMethod m) noexcept;

// =============================================================================
// ParamError
// =============================================================================

/**
 * @brief Error codes for `Request::param<T>()`.
 *
 * Returned via `std::expected`. Never thrown.
 */
enum class ParamError : std::uint8_t
{
    NotFound,      ///< No path parameter with the given name was captured by the router.
    BadConversion, ///< The raw string could not be converted to the requested type T.
};

// =============================================================================
// Request
// =============================================================================

namespace detail {
class ConnectionHandler; // forward declaration — the only authorized constructor caller
struct ParsedRequest;    // forward declaration — full definition in src/http/http_parser.hpp
} // namespace detail

/**
 * @brief Immutable view of an HTTP/1.1 request, valid for the handler lifetime.
 *
 * `Request` exposes all HTTP request data — method, path, query string, headers,
 * body, and typed path parameters — through a clean, type-safe interface.
 * All string fields are zero-copy `std::string_view` into the owned read buffer.
 *
 * **Ownership:**
 * `Request::Impl` owns both the raw TCP read buffer (`std::vector<std::byte>`)
 * and the `ParsedRequest` derived from it. This ensures that all `std::string_view`
 * fields remain valid for the entire handler lifetime without any copies.
 *
 * **Middleware context store:**
 * `req.set(key, value)` and `req.get<T>(key)` provide a type-erased key/value
 * bag for middleware-to-handler communication (e.g. `"auth.user"`). The store uses
 * `std::any` internally; `get<T>()` returns `std::nullopt` if the key is absent or
 * the stored type does not match T exactly.
 *
 * **Thread-safety:**
 * Not thread-safe. Must be used on the connection strand only.
 *
 * **Move semantics:**
 * Move-only. A moved-from `Request` is valid but empty: `valid() == false`.
 * Calling any accessor on a moved-from `Request` is undefined behaviour.
 */
class Request
{
public:
    // Non-copyable — the owned buffer must not be duplicated silently.
    Request(const Request&)            = delete;
    Request& operator=(const Request&) = delete;

    // Move ctor/assign/dtor declared here, defined out-of-line in request_impl.cpp.
    // This is required because std::unique_ptr<Impl> needs Impl to be complete at
    // the point the defaulted special members are instantiated, but Impl is
    // deliberately incomplete in this public header. (DEVIATION — pre-approved.)
    Request(Request&&) noexcept;
    Request& operator=(Request&&) noexcept;
    ~Request();

    /**
     * @brief Returns true if this Request holds valid parsed data.
     *
     * Returns false after the Request has been moved from.
     *
     * @return `true` when the underlying Impl is present and the request is usable.
     */
    [[nodiscard]] bool valid() const noexcept;

    // -------------------------------------------------------------------------
    // Request line accessors
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the HTTP method of this request.
     *
     * @return `HttpMethod` enum value. `HttpMethod::Unknown` for unrecognised verbs.
     * @note Zero-copy. Returns in O(1).
     */
    [[nodiscard]] HttpMethod method() const noexcept;

    /**
     * @brief Returns the request path (without query string).
     *
     * For a request to `/users/42?sort=asc` this returns `/users/42`.
     * The returned view is zero-copy into the owned buffer; valid for
     * the lifetime of this `Request`.
     *
     * @return Path component of the request target, never empty (at minimum `/`).
     */
    [[nodiscard]] std::string_view path() const noexcept;

    /**
     * @brief Returns the raw query string (without the leading `?`).
     *
     * For `/users?sort=asc&page=2` this returns `sort=asc&page=2`.
     * Returns an empty `std::string_view` if no query string is present.
     *
     * The returned view is zero-copy into the owned buffer; valid for
     * the lifetime of this `Request`.
     *
     * @return Raw query string, or empty string_view if absent.
     */
    [[nodiscard]] std::string_view query() const noexcept;

    // -------------------------------------------------------------------------
    // Header access
    // -------------------------------------------------------------------------

    /**
     * @brief Retrieves a header value by name (case-insensitive lookup).
     *
     * HTTP headers are case-insensitive per RFC 7230 §3.2. Lookup is performed
     * by comparing lowercased input against lowercased stored names — the stored
     * names preserve original casing from the wire for round-trip fidelity.
     *
     * When multiple headers share the same name (e.g. `Set-Cookie`), the
     * implementation returns the value of the first occurrence. Handling of
     * duplicate headers beyond the first is deferred to a future task.
     *
     * @param name  Header field name. Case-insensitive (e.g. `"content-type"`,
     *              `"Content-Type"`, and `"CONTENT-TYPE"` all match).
     * @return      `std::optional<std::string_view>` containing the header value
     *              if found, or `std::nullopt` if the header is absent.
     *              The returned view is zero-copy into the owned buffer.
     * @note        `[[nodiscard]]` — silently discarding the optional is almost always a bug.
     */
    [[nodiscard]] std::optional<std::string_view> header(std::string_view name) const noexcept;

    // -------------------------------------------------------------------------
    // Body access
    // -------------------------------------------------------------------------

    /**
     * @brief Returns a non-owning view of the raw request body bytes.
     *
     * For Content-Length bodies and chunked transfer-encoded bodies alike,
     * `body()` returns a contiguous span of the fully assembled bytes. The span
     * is valid for the lifetime of this `Request`.
     *
     * @return Span of body bytes. Empty for requests with no body (e.g. GET).
     */
    [[nodiscard]] std::span<const std::byte> body() const noexcept;

    // -------------------------------------------------------------------------
    // Typed path parameter extraction
    // -------------------------------------------------------------------------

    /**
     * @brief Retrieves a typed path parameter by name.
     *
     * Converts the raw string parameter captured during routing to type `T`
     * using `std::from_chars` for arithmetic types and string passthrough for
     * string types. Returns an error if the parameter is absent or conversion fails.
     *
     * Conversion rules:
     * - `std::integral<T>` or `std::floating_point<T>`: converted via `std::from_chars`.
     *   Returns `ParamError::BadConversion` if the entire raw value cannot be parsed as T.
     * - `std::string_view`: returned zero-copy; lifetime tied to the `Request` lifetime.
     * - `std::string`: returned as an owning copy.
     *
     * Path parameters are populated by the Router via `Request::set_params()`
     * before the handler is invoked. Calling `param<T>()` before the router has
     * dispatched is defined behaviour: it returns `ParamError::NotFound`.
     *
     * @tparam T  Target type. Must satisfy `aevox::ParamConvertible`.
     * @param  name  Parameter name as registered in the route pattern (case-sensitive).
     * @return       The converted value on success.
     *               `ParamError::NotFound` if no parameter with this name was captured.
     *               `ParamError::BadConversion` if the raw string cannot convert to T.
     * @note         Zero-copy for `std::string_view` — lifetime tied to the connection buffer.
     *               Copy to `std::string` if ownership past the handler return is required.
     */
    template <typename T>
        requires aevox::ParamConvertible<T>
    [[nodiscard]] std::expected<T, ParamError> param(std::string_view name) const noexcept;

    // -------------------------------------------------------------------------
    // Async JSON body parsing
    // -------------------------------------------------------------------------

    /**
     * @brief Deserializes the request body as JSON into type `T`.
     *
     * Reads the raw body bytes held by this request and passes them to the
     * active `aevox::JsonBackend`. The backend performs compile-time schema
     * inference — no runtime type map is required. For `GlazeBackend`, any
     * struct with public fields is automatically reflectable.
     *
     * @tparam T  Target type. Must be default-constructible and satisfy the
     *            active backend's deserialization requirements. For
     *            `GlazeBackend`: any aggregate struct or standard container
     *            with public fields. Compilation fails for non-reflectable
     *            types at the `GlazeBackend::deserialize<T>` call site.
     * @return    `Task` resolving to `std::expected<T, aevox::JsonError>`.
     *            The error branch is populated on parse failure, missing
     *            required fields, type mismatches, or invalid UTF-8. The
     *            `aevox::JsonError::message()` contains a human-readable
     *            description of the failure.
     * @note      Thread-safety: safe to call concurrently on separate
     *            `Request` instances. Not safe to call from multiple threads
     *            on the same `Request` instance.
     * @note      Body caching: the body bytes are stable for the full
     *            request lifetime (owned by `Request::Impl`). Each call to
     *            `json<T>()` re-parses the body bytes. If the same type is
     *            needed multiple times, cache the result in the handler.
     *            Calling with two different types `T` and `U` on the same
     *            request is supported and parses twice.
     * @note      The coroutine suspends and resumes synchronously (no I/O
     *            is performed). The `co_await` is required for consistency
     *            with the async handler signature.
     * @throws    Nothing. All errors surface via `std::unexpected`.
     */
    template <typename T>
        requires aevox::Deserializable<T>
    [[nodiscard]] aevox::Task<std::expected<T, aevox::JsonError>> json() const;

    // -------------------------------------------------------------------------
    // Middleware context store
    // -------------------------------------------------------------------------

    /**
     * @brief Stores a typed value in the per-request middleware context bag.
     *
     * Used by middleware to pass typed data to downstream handlers or other
     * middleware layers. The value is stored type-erased via `std::any`.
     *
     * @tparam T  Any copyable or movable type.
     * @param  key    String key identifying this context slot (e.g. `"auth.user"`).
     *                If a value already exists under this key it is overwritten.
     * @param  value  Value to store. Stored by move if possible, else by copy.
     * @note   Thread-safety: same as `Request` — not thread-safe.
     */
    template <typename T> void set(std::string_view key, T&& value);

    /**
     * @brief Retrieves a typed value from the per-request middleware context bag.
     *
     * Returns `std::nullopt` if no value is stored under `key`, or if the stored
     * value's `std::any::type()` does not exactly match `T`. Exact type match is
     * required — no implicit conversions are applied.
     *
     * @tparam T  The type that was originally passed to `set<T>()`.
     * @param  key  String key identifying the context slot.
     * @return     `std::optional<T>` with the stored value, or `std::nullopt`.
     * @note   Zero-cost absent case — no exception, no RTTI beyond `std::any`.
     */
    template <typename T> [[nodiscard]] std::optional<T> get(std::string_view key) const;

    // -------------------------------------------------------------------------
    // WebSocket upgrade
    // -------------------------------------------------------------------------

    /**
     * @brief Returns `true` if this request carries the WebSocket upgrade headers.
     *
     * Checks for the presence of all three required headers:
     * - `Upgrade: websocket` (case-insensitive value)
     * - `Connection: Upgrade` (case-insensitive value contains "upgrade")
     * - `Sec-WebSocket-Key` (any non-empty value)
     *
     * This predicate is a cheap synchronous check — it performs no I/O.
     * The full validity of the key is verified only inside
     * `upgrade_websocket()`.
     *
     * @return `true` when all three headers are present and well-formed.
     * @note   noexcept — never allocates.
     */
    [[nodiscard]] bool is_websocket_upgrade() const noexcept;

    /**
     * @brief Performs the HTTP/1.1 to WebSocket upgrade handshake.
     *
     * Validates the upgrade headers, computes `Sec-WebSocket-Accept`, writes
     * the HTTP 101 response to the underlying `TcpStream`, and transfers
     * ownership of the socket to a new `WebSocketSession`.
     *
     * On success the `Request`'s underlying `TcpStream` is consumed — the
     * connection now belongs to the returned `WebSocket`. After `co_await`ing
     * this method the `Request` must not be used for any further HTTP I/O.
     *
     * On failure a `WebSocketError` is returned and the connection is still in
     * HTTP mode — the caller may write an HTTP error response (e.g. 400) and
     * close normally.
     *
     * @return `Task<std::expected<WebSocket, WebSocketError>>`.
     *         Success: fully constructed `WebSocket` ready for `send()`.
     *         `WebSocketError::invalid_handshake` if upgrade headers are
     *         absent or malformed.
     *         `WebSocketError::send_failed` if writing the 101 response failed.
     * @note  Must be called from the connection coroutine — the same strand
     *        that owns this `Request`. Not thread-safe.
     * @note  The `Request` is in an unspecified state after a successful
     *        upgrade; do not call any other method on it.
     * @throws Nothing. All errors surface via `std::unexpected`.
     */
    [[nodiscard]] aevox::Task<std::expected<aevox::WebSocket, aevox::WebSocketError>>
    upgrade_websocket();

private:
    // Impl is private — the full layout is defined in src/http/request_impl.hpp.
    // Application code sees only the incomplete type here and cannot name it.
    // Framework-internal code (and tests) that include request_impl.hpp obtain
    // the complete struct and may construct Request::Impl directly.
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Only ConnectionHandler in src/http/ may construct a Request.
    explicit Request(std::unique_ptr<Impl> impl) noexcept;
    friend class detail::ConnectionHandler;

    // The Router injects captured path parameters directly via friend-class
    // access: req.impl_->params = std::move(params). No set_params() method is
    // needed — friend class access covers all private members including impl_.
    friend class Router; // forward declared; defined in router.hpp

    // Internal factory helpers (defined in src/http/request_impl.hpp).
    // Declared without namespace qualifier because a qualified friend requires
    // prior declaration in that namespace, which is impossible here (Impl is still
    // incomplete at namespace scope). Resolved to aevox::make_request_from_impl /
    // aevox::get_request_impl / aevox::get_mutable_request_impl by ADL once the
    // definitions are visible.

    /// Takes ownership of a pre-built Impl. Used by ConnectionHandler.
    friend Request make_request_from_impl(std::unique_ptr<Impl>) noexcept;

    /// Constructs Impl from raw parts (buffer + parsed request). Used by tests.
    /// params are NOT injected here — call get_mutable_request_impl() to set them.
    /// Not noexcept — std::make_unique<Impl> may throw std::bad_alloc.
    friend Request make_request_from_impl(std::vector<std::byte>, detail::ParsedRequest);

    /// Read-only Impl access for internal inspection (tests, Router).
    friend const Impl* get_request_impl(const Request&) noexcept;

    /// Mutable Impl access for internal param injection (tests, Router).
    /// The Router uses req.impl_->params = ... directly via friend class Router.
    friend Impl* get_mutable_request_impl(Request&) noexcept;
};

} // namespace aevox
