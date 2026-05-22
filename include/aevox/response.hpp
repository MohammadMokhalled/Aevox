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
// The streaming write API is not yet designed. In v0.1, stream() creates a non-streaming Response.
// See Tasks/architecture/AEV-005-arch.md §10.2.
//
// Design: Tasks/architecture/AEV-005-arch.md §3.3

#include <aevox/concepts.hpp>
#include <aevox/json_error.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace aevox {

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
class Response
{
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
    [[nodiscard]] std::optional<std::string_view> get_header(std::string_view name) const noexcept;

    // -------------------------------------------------------------------------
    // Fluent builder — lvalue and rvalue overloads
    // -------------------------------------------------------------------------

    /**
     * @brief Sets the Content-Type header (lvalue overload — modifies in place).
     *
     * @param ct  MIME type string (e.g. `"application/json"`, `"text/plain"`).
     * @return    Reference to this Response for chaining.
     */
    [[nodiscard]] Response& content_type(std::string_view ct) &;

    /**
     * @brief Sets the Content-Type header (rvalue overload — consumes and returns).
     *
     * Enables efficient chaining on temporaries:
     * `Response::ok("body").content_type("text/html")`.
     *
     * @param ct  MIME type string.
     * @return    This Response by value (moved).
     */
    [[nodiscard]] Response content_type(std::string_view ct) &&;

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
    [[nodiscard]] Response& header(std::string_view name, std::string_view value) &;

    /**
     * @brief Sets an arbitrary response header (rvalue overload).
     *
     * @param name   Header field name.
     * @param value  Header field value.
     * @return       This Response by value (moved).
     */
    [[nodiscard]] Response header(std::string_view name, std::string_view value) &&;

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
     * @note        Sets Content-Type: text/plain by default. Override with
     *              `.content_type("application/json")` or similar if needed.
     */
    [[nodiscard]] static Response created(std::string_view body = {});

    /**
     * @brief Creates a 404 Not Found response.
     *
     * @param body  Optional response body. Defaults to empty.
     * @return      Response with status 404.
     * @note        Sets Content-Type: text/plain by default. Override with
     *              `.content_type(...)` if the body is structured (e.g. JSON).
     */
    [[nodiscard]] static Response not_found(std::string_view body = {});

    /**
     * @brief Creates a 400 Bad Request response.
     *
     * @param body  Optional diagnostic message. Defaults to empty.
     * @return      Response with status 400.
     * @note        Sets Content-Type: text/plain by default. Override with
     *              `.content_type(...)` if the body is structured (e.g. JSON).
     */
    [[nodiscard]] static Response bad_request(std::string_view body = {});

    /**
     * @brief Creates a 405 Method Not Allowed response.
     *
     * Sets Content-Type: text/plain. The caller is responsible for adding the
     * `Allow` header listing the permitted methods via `.header("Allow", value)`:
     * @code
     * co_return Response::method_not_allowed().header("Allow", "GET, POST");
     * @endcode
     *
     * @param body  Optional diagnostic message. Defaults to empty.
     * @return      Response with status 405.
     */
    [[nodiscard]] static Response method_not_allowed(std::string_view body = {});

    /**
     * @brief Creates a 401 Unauthorized response.
     *
     * @param body  Optional response body. Defaults to empty.
     * @return      Response with status 401.
     * @note        Sets Content-Type: text/plain by default. Override with
     *              `.content_type(...)` if the body is structured (e.g. JSON).
     */
    [[nodiscard]] static Response unauthorized(std::string_view body = {});

    /**
     * @brief Creates a 403 Forbidden response.
     *
     * @param body  Optional response body. Defaults to empty.
     * @return      Response with status 403.
     * @note        Sets Content-Type: text/plain by default. Override with
     *              `.content_type(...)` if the body is structured (e.g. JSON).
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
     * Calls the active `aevox::JsonBackend::serialize()` synchronously.
     * Serialization is CPU-bound work with no I/O; glaze serialization of
     * typical API structs completes in microseconds. If the application
     * serializes extremely large bodies (>1 MB), it should offload to
     * `co_await aevox::pool(...)` before calling this factory.
     *
     * On serialization failure, returns a 500 Internal Server Error response
     * with a JSON body `{"error":"json_serialization_failed","detail":"..."}`,
     * where `"detail"` contains the `JsonError::message()` string. This
     * ensures the function always returns a valid `Response` without throwing.
     *
     * @tparam T  Type to serialize. Must satisfy `aevox::Serializable`.
     *            For `GlazeBackend`: any aggregate struct or standard
     *            container with public fields. Compilation fails for
     *            non-reflectable types at the `GlazeBackend::serialize<T>`
     *            call site.
     * @param  value  The value to serialize. Accepts lvalue or rvalue.
     * @return        `Response` with status 200 and
     *                `Content-Type: application/json` on success.
     *                `Response` with status 500 and a JSON error body on
     *                serialization failure (see above).
     * @note   Thread-safety: `GlazeBackend` carries no mutable state;
     *         safe to call concurrently from separate handlers.
     * @note   `value` is taken by const-ref. Both lvalues and rvalues bind.
     * @throws Nothing. Serialization errors surface as a 500 response.
     */
    template <typename T>
        requires aevox::Serializable<T>
    [[nodiscard]] static Response json(const T& value);

    /**
     * @brief Creates a streaming response sentinel with the given Content-Type.
     *
     * In v0.1 this returns a normal (non-streaming) `Response` with status 200,
     * the provided `Content-Type`, and an empty body. The streaming write API
     * (`stream.write(...)`) is not yet designed. This factory method exists
     * now so that v0.1 code compiles; streaming behaviour is deferred.
     *
     * @param content_type  Content-Type for the stream (e.g. `"text/event-stream"`).
     * @return              Response with status 200 and the given Content-Type.
     *                      Body is empty; streaming write path is a no-op in v0.1.
     */
    [[nodiscard]] static Response stream(std::string_view content_type);

    /**
     * @brief Creates a 101 Switching Protocols sentinel response.
     *
     * Used internally by the WebSocket upgrade path to signal that the HTTP
     * connection has been handed off to a WebSocket session. The connection
     * handler checks for status 101 and suppresses writing this response to
     * the socket (the real HTTP 101 was already sent by `upgrade_websocket()`).
     *
     * @return Response with status 101 and no body.
     * @note   Application code should never need to call this method directly.
     *         Use `co_await req.upgrade_websocket()` instead.
     */
    [[nodiscard]] static Response switching_protocols();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Private constructor — used only by factory methods.
    explicit Response(int status_code, std::string body, std::string content_type_value);

    // Internal read-only Impl access for serialization (app_impl.cpp).
    // Declared here, defined inline in src/http/response_impl.hpp.
    friend std::optional<std::reference_wrapper<const Impl>> get_response_impl(
        const Response&) noexcept;
};

} // namespace aevox
