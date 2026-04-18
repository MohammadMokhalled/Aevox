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
// Design: AEV-005-arch.md §3.2

#include <aevox/concepts.hpp>
#include <aevox/task.hpp>

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

// =============================================================================
// HttpMethod
// =============================================================================

/**
 * @brief Typed enumeration of HTTP/1.1 request methods supported by Aevox v0.1.
 *
 * Returned by `Request::method()`. The parser maps the raw method string to
 * this enum — unknown verbs yield `HttpMethod::Unknown`.
 */
enum class HttpMethod : std::uint8_t {
    GET,
    POST,
    PUT,
    PATCH,
    DELETE,
    HEAD,
    OPTIONS,
    Unknown, ///< Any verb not listed above (treated as a client error by the router).
};

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
enum class ParamError : std::uint8_t {
    NotFound,      ///< No path parameter with the given name was captured by the router.
    BadConversion, ///< The raw string could not be converted to the requested type T.
};

// =============================================================================
// BodyParseError
// =============================================================================

/**
 * @brief Error codes for `Request::json<T>()`.
 *
 * `NotImplemented` is the only value returned in v0.1. AEV-009 extends this.
 */
enum class BodyParseError : std::uint8_t {
    NotImplemented, ///< JSON parsing is not wired in v0.1; replaced by AEV-009.
    BadJson,        ///< The body is not valid JSON (reserved for AEV-009).
    TypeMismatch,   ///< JSON does not match the target type schema (reserved for AEV-009).
};

// =============================================================================
// Request
// =============================================================================

namespace detail {
class ConnectionHandler; // forward declaration — the only authorized constructor caller
struct ParsedRequest;   // forward declaration — full definition in src/http/http_parser.hpp
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
class Request {
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
    [[nodiscard]] std::optional<std::string_view>
    header(std::string_view name) const noexcept;

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
     * Path parameters are populated by the Router (AEV-004) via `Request::set_params()`
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
    [[nodiscard]] std::expected<T, ParamError>
    param(std::string_view name) const noexcept;

    // -------------------------------------------------------------------------
    // Async JSON body parsing
    // -------------------------------------------------------------------------

    /**
     * @brief Asynchronously parses the request body as JSON into type `T`.
     *
     * In v0.1 this always returns `BodyParseError::NotImplemented`. AEV-009
     * replaces the implementation stub with a real glaze-backed deserializer.
     *
     * The coroutine suspends immediately and resumes with the error in v0.1.
     * AEV-004 handlers must `co_await` this — it is not synchronous.
     *
     * @tparam T  Target deserialization type. Must satisfy `aevox::Deserializable`.
     *            The constraint is a stub in v0.1 (always satisfied).
     * @return    `Task<std::expected<T, BodyParseError>>` — always resolves to
     *            `BodyParseError::NotImplemented` in v0.1.
     * @note      The implementation hook for AEV-009 is `Request::Impl::do_json_parse()`.
     *            See AEV-005-arch.md §4.3.
     */
    template <typename T>
        requires aevox::Deserializable<T>
    [[nodiscard]] aevox::Task<std::expected<T, BodyParseError>>
    json() const;

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
    template <typename T>
    void set(std::string_view key, T&& value);

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
    template <typename T>
    [[nodiscard]] std::optional<T> get(std::string_view key) const;

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

    // AEV-004 Router injects captured path parameters directly via friend-class
    // access: req.impl_->params = std::move(params). No set_params() method is
    // needed — friend class access covers all private members including impl_.
    friend class Router; // AEV-004 — forward declared; defined in AEV-004 ADD

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
    friend Request make_request_from_impl(
        std::vector<std::byte>,
        detail::ParsedRequest);

    /// Read-only Impl access for internal inspection (tests, AEV-004).
    friend const Impl* get_request_impl(const Request&) noexcept;

    /// Mutable Impl access for internal param injection (tests, AEV-004 Router).
    /// AEV-004 uses req.impl_->params = ... directly via friend class Router.
    friend Impl* get_mutable_request_impl(Request&) noexcept;
};

} // namespace aevox
