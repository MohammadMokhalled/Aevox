#pragma once
// include/aevox/response.hpp
//
// Public aevox::Response class — the primary write interface for HTTP handlers.
//
// Ownership: Response owns its headers and body string internally via Impl.
//
// Thread-safety: Not thread-safe. Response is a value type created in one
// coroutine and returned (by move) to the framework for serialization.
// Never share a Response between threads.
//
// Move semantics: Move-only. A moved-from Response is valid but empty
// (status_code() == 0, body is empty, no headers).
//
// Construction: Only via static factory methods — no public constructor.
//
// Streaming: Response::stream() returns a Response with status 200 and the
// given Content-Type but an empty body. The actual streaming write API
// is designed in AEV-006. In v0.1, stream() creates a non-streaming Response.
// See AEV-005-arch.md §10.2.
//
// Design: AEV-005-arch.md §3.3

#include <aevox/concepts.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace aevox {

/**
 * @brief Error codes for `Response::json<T>()` serialization.
 *
 * `NotImplemented` is the only value produced in v0.1. AEV-009 extends this.
 */
enum class SerializeError : std::uint8_t {
    NotImplemented,   ///< JSON serialization is not wired in v0.1; replaced by AEV-009.
    TypeNotSupported, ///< The type T cannot be serialized (reserved for AEV-009).
};

/**
 * @brief A fully formed HTTP/1.1 response, ready for serialization.
 *
 * `Response` holds the status code, header map, and body for one HTTP response.
 * It is created exclusively via static factory methods and returned (by move)
 * from route handlers and middleware.
 *
 * The framework serializes `Response` to wire bytes in `detail::ConnectionHandler`.
 * `Response` itself never writes to a socket.
 *
 * **Ownership:**
 * Owns all header values and the body string. No views into external buffers.
 *
 * **Thread-safety:**
 * Not thread-safe. `Response` is a value type — create it, move it out of the
 * handler, and let the framework write it. Never share a `Response`.
 *
 * **Move semantics:**
 * Move-only. A moved-from `Response` is valid but empty: `status_code() == 0`,
 * headers empty, body empty.
 */
class Response {
public:
    Response(const Response&)            = delete;
    Response& operator=(const Response&) = delete;

    // Move ctor/assign/dtor declared here, defined out-of-line in response_impl.cpp.
    // This is required because std::unique_ptr<Impl> needs Impl to be complete at
    // the point the defaulted special members are instantiated, but Impl is
    // deliberately incomplete in this public header. (DEVIATION — pre-approved.)
    Response(Response&&) noexcept;
    Response& operator=(Response&&) noexcept;
    ~Response();

    // -------------------------------------------------------------------------
    // Status and body accessors (used by the framework's write path)
    // -------------------------------------------------------------------------

    /**
     * @brief Returns the HTTP status code of this response.
     *
     * @return Integer HTTP status code (e.g. 200, 404). Returns 0 for a
     *         moved-from Response.
     */
    [[nodiscard]] int status_code() const noexcept;

    /**
     * @brief Returns a read-only view of the response body.
     *
     * @return View into the owned body string. Valid for the lifetime of
     *         this Response. Empty for responses with no body.
     */
    [[nodiscard]] std::string_view body_view() const noexcept;

    /**
     * @brief Retrieves a response header value by exact name.
     *
     * Header names are stored as provided (no normalization). Lookup is
     * exact-match (case-sensitive), unlike `Request::header()` which is
     * case-insensitive. The framework's write path uses this to read headers
     * during serialization.
     *
     * @param name  Header field name (e.g. `"Content-Type"`).
     * @return      `std::optional<std::string_view>` containing the value,
     *              or `std::nullopt` if the header is not set.
     * @note        The returned view is valid for the lifetime of this Response.
     */
    [[nodiscard]] std::optional<std::string_view>
    get_header(std::string_view name) const noexcept;

    // -------------------------------------------------------------------------
    // Fluent builder — lvalue and rvalue overloads
    // -------------------------------------------------------------------------

    /**
     * @brief Sets the Content-Type header (lvalue overload — modifies in place).
     *
     * @param ct  MIME type string (e.g. `"application/json"`, `"text/plain"`).
     * @return    Reference to this Response for chaining.
     */
    Response& content_type(std::string_view ct) &;

    /**
     * @brief Sets the Content-Type header (rvalue overload — consumes and returns).
     *
     * Enables efficient chaining on temporaries:
     * `Response::ok("body").content_type("text/html")`.
     *
     * @param ct  MIME type string.
     * @return    This Response by value (moved).
     */
    [[nodiscard]] Response  content_type(std::string_view ct) &&;

    /**
     * @brief Sets an arbitrary response header (lvalue overload).
     *
     * If a header with the same name already exists it is overwritten.
     * Header names are stored as provided — the framework normalizes them
     * during wire serialization.
     *
     * @param name   Header field name (e.g. `"X-Request-Id"`).
     * @param value  Header field value.
     * @return       Reference to this Response for chaining.
     */
    Response& header(std::string_view name, std::string_view value) &;

    /**
     * @brief Sets an arbitrary response header (rvalue overload).
     *
     * @param name   Header field name.
     * @param value  Header field value.
     * @return       This Response by value (moved).
     */
    [[nodiscard]] Response  header(std::string_view name, std::string_view value) &&;

    // -------------------------------------------------------------------------
    // Static factory methods
    // -------------------------------------------------------------------------

    /**
     * @brief Creates a 200 OK response.
     *
     * @param body  Optional response body. Defaults to empty.
     *              Content-Type is `text/plain` unless overridden by chaining
     *              `.content_type(...)`.
     * @return      Response with status 200.
     */
    [[nodiscard]] static Response ok(std::string_view body = {});

    /**
     * @brief Creates a 201 Created response.
     *
     * @param body  Optional response body. Defaults to empty.
     * @return      Response with status 201.
     */
    [[nodiscard]] static Response created(std::string_view body = {});

    /**
     * @brief Creates a 404 Not Found response.
     *
     * @param body  Optional response body. Defaults to empty.
     * @return      Response with status 404.
     */
    [[nodiscard]] static Response not_found(std::string_view body = {});

    /**
     * @brief Creates a 400 Bad Request response.
     *
     * @param body  Optional diagnostic message. Defaults to empty.
     * @return      Response with status 400.
     */
    [[nodiscard]] static Response bad_request(std::string_view body = {});

    /**
     * @brief Creates a 401 Unauthorized response.
     *
     * @param body  Optional response body. Defaults to empty.
     * @return      Response with status 401.
     */
    [[nodiscard]] static Response unauthorized(std::string_view body = {});

    /**
     * @brief Creates a 403 Forbidden response.
     *
     * @param body  Optional response body. Defaults to empty.
     * @return      Response with status 403.
     */
    [[nodiscard]] static Response forbidden(std::string_view body = {});

    /**
     * @brief Creates a 200 OK JSON response from a pre-serialized JSON string.
     *
     * Sets `Content-Type: application/json` and status 200.
     *
     * This non-template overload always works in v0.1. Use it when you already
     * have a serialized JSON string (from a third-party library, a test fixture,
     * or manual construction).
     *
     * @param body  Serialized JSON string (ownership is taken by move).
     * @return      Response with status 200 and `Content-Type: application/json`.
     */
    [[nodiscard]] static Response json(std::string body);

    /**
     * @brief Creates a 200 OK JSON response by serializing `value` to JSON.
     *
     * In v0.1 this always returns a `Response` whose body is the error sentinel
     * string `{\"error\":\"not_implemented\"}`. AEV-009 replaces the stub
     * with real glaze serialization.
     *
     * @tparam T  Type to serialize. Must satisfy `aevox::Serializable`.
     *            The constraint is a stub in v0.1 (always satisfied).
     * @param  value  The value to serialize. Moved from the caller.
     * @return        Response with status 200 and `Content-Type: application/json`.
     *                Body content is a stub in v0.1.
     * @note          AEV-009's hook is `Response::Impl::do_json_serialize()`. See
     *                AEV-005-arch.md §4.3.
     */
    template <typename T>
        requires aevox::Serializable<T>
    [[nodiscard]] static Response json(T&& value);

    /**
     * @brief Creates a streaming response sentinel with the given Content-Type.
     *
     * In v0.1 this returns a normal (non-streaming) `Response` with status 200,
     * the provided `Content-Type`, and an empty body. The streaming write API
     * (`stream.write(...)`) is designed in AEV-006. This factory method exists
     * now so that v0.1 code compiles; streaming behaviour is deferred.
     *
     * @param content_type  Content-Type for the stream (e.g. `"text/event-stream"`).
     * @return              Response with status 200 and the given Content-Type.
     *                      Body is empty; streaming write path is a no-op in v0.1.
     */
    [[nodiscard]] static Response stream(std::string_view content_type);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Private constructor — used only by factory methods.
    explicit Response(int status_code, std::string body, std::string content_type_value);
};

} // namespace aevox
