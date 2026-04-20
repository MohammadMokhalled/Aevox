// Request and Response unit tests — no I/O.
// ADD ref: Tasks/architecture/AEV-005-arch.md §8
//
// Tests construct Request objects directly via the internal PIMPL, bypassing
// the ConnectionHandler friend, by including src/http/request_impl.hpp which
// makes Request::Impl and the private constructor visible in this TU.
// This is an intentional framework-internal test pattern (see ADD §8).

#include <aevox/request.hpp>
#include <aevox/response.hpp>

#include <catch2/catch_test_macros.hpp>

// Internal headers — gives access to Request::Impl and template definitions.
#include <coroutine>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "http/request_impl.hpp"
#include "http/response_impl.hpp"

// =============================================================================
// Test helpers
// =============================================================================

/// Converts a string literal to a vector of bytes (owned buffer).
static std::vector<std::byte> make_buffer(std::string_view s)
{
    std::vector<std::byte> buf(s.size());
    std::memcpy(buf.data(), s.data(), s.size());
    return buf;
}

/// Reinterprets a byte buffer as a string_view without copying.
/// std::byte* → const char* is well-defined per [basic.types]/2:
/// any object may be accessed through a pointer to char or unsigned char,
/// and std::byte has the same aliasing permissions.
static std::string_view bytes_as_string_view(const std::vector<std::byte>& v) noexcept
{
    return {reinterpret_cast<const char*>(v.data()),
            v.size()}; // NOLINT: well-defined per [basic.types]/2
}

/// Constructs a test Request with the given buffer, parsed request, and params.
///
/// Two-step approach (M2 fix — avoids naming aevox::Request::Impl in this TU):
///   1. make_request_from_impl(buffer, parsed) constructs Impl internally via
///      the friend function defined in request_impl.hpp.
///   2. get_mutable_request_impl() returns a non-const Impl pointer (friend fn)
///      so we can inject the path params after construction without naming Impl.
static aevox::Request make_test_request(std::vector<std::byte>                       buffer,
                                        aevox::detail::ParsedRequest                 parsed,
                                        std::unordered_map<std::string, std::string> params = {})
{
    auto req = aevox::make_request_from_impl(std::move(buffer), std::move(parsed));
    if (!params.empty()) {
        auto* impl   = aevox::get_mutable_request_impl(req);
        impl->params = std::move(params);
    }
    return req;
}

/// Drives a lazy Task<T> to completion synchronously and returns its result.
///
/// Task<T> is lazy (initial_suspend = suspend_always): the coroutine body does
/// not run until resume() is called. This helper:
///   1. Calls await_suspend(noop_coroutine()) which registers a noop continuation
///      and returns the inner coroutine handle (via symmetric transfer).
///   2. Resumes the inner handle, running the coroutine body to completion.
///   3. Calls await_resume() to retrieve the stored result.
///
/// This works for the v0.1 json<T>() stub because the body co_returns immediately
/// without suspending. Do not use this helper for Tasks that genuinely suspend on
/// I/O — it would block the calling thread.
template <typename T> static T drive_task(aevox::Task<T> task)
{
    // Task is lazy (initial_suspend = suspend_always). Steps:
    //   1. await_suspend(noop_coroutine()) registers noop as continuation
    //      and returns the inner coroutine handle (symmetric transfer target).
    //   2. Resume the inner handle. The coroutine body runs. On co_return,
    //      final_suspend fires FinalAwaitable which tail-calls noop_coroutine.
    //   3. await_resume() reads the stored result from the promise.
    auto inner = task.await_suspend(std::noop_coroutine());
    inner.resume();
    return task.await_resume();
}

// =============================================================================
// Request tests — header access
// =============================================================================

TEST_CASE("Request - header access - happy path, existing header returns value",
          "[http][request]")
{
    auto buf = make_buffer("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");

    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    pr.target = "/";
    // Simulate a header stored with a view into the buffer.
    // For the test we store header names/values as string_view into a local string
    // (the test controls lifetime — same shape as production where parser owns the buf).
    static const std::string hname{"Host"};
    static const std::string hval{"example.com"};
    pr.headers.emplace_back(std::string_view{hname}, std::string_view{hval});

    auto req    = make_test_request(std::move(buf), std::move(pr));
    auto result = req.header("Host");

    REQUIRE(result.has_value());
    CHECK(*result == "example.com");
}

TEST_CASE("Request - header access - missing header returns nullopt", "[http][request]")
{
    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    pr.target = "/";
    // No headers.

    auto req    = make_test_request({}, std::move(pr));
    auto result = req.header("Authorization");

    CHECK(!result.has_value());
}

TEST_CASE("Request - header access - lookup is case-insensitive", "[http][request]")
{
    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    pr.target = "/";
    static const std::string hname{"Content-Type"};
    static const std::string hval{"application/json"};
    pr.headers.emplace_back(std::string_view{hname}, std::string_view{hval});

    auto req = make_test_request({}, std::move(pr));

    // All three casings must return the same value.
    auto r1 = req.header("content-type");
    auto r2 = req.header("CONTENT-TYPE");
    auto r3 = req.header("cOnTeNt-TyPe");

    REQUIRE(r1.has_value());
    REQUIRE(r2.has_value());
    REQUIRE(r3.has_value());
    CHECK(*r1 == "application/json");
    CHECK(*r2 == "application/json");
    CHECK(*r3 == "application/json");
}

// =============================================================================
// Request tests — param<T>()
// =============================================================================

TEST_CASE("Request - param<int> - happy path converts correctly", "[http][request]")
{
    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    pr.target = "/users/42";

    std::unordered_map<std::string, std::string> params;
    params["id"] = "42";

    auto req    = make_test_request({}, std::move(pr), std::move(params));
    auto result = req.param<int>("id");

    REQUIRE(result.has_value());
    CHECK(*result == 42);
}

TEST_CASE("Request - param<string_view> - zero-copy, no allocation", "[http][request]")
{
    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    pr.target = "/";

    // Use a string long enough to bypass SSO (≥ 32 chars) so the std::string
    // data pointer is a stable heap address.
    std::unordered_map<std::string, std::string> params;
    params["token"] = "abcdefghijklmnopqrstuvwxyz012345"; // 32 chars

    auto req = make_test_request({}, std::move(pr), std::move(params));

    auto result = req.param<std::string_view>("token");
    REQUIRE(result.has_value());
    CHECK(*result == "abcdefghijklmnopqrstuvwxyz012345");

    // Zero-copy verification: the string_view data pointer must equal the
    // address of the string stored inside the params map in Impl.
    // Use auto to avoid naming aevox::Request::Impl (which is private after M2 fix).
    // get_request_impl is a friend function returning const Request::Impl*.
    const auto* impl = aevox::get_request_impl(req);
    REQUIRE(impl != nullptr);
    const std::string& stored = impl->params.at("token");
    CHECK(result->data() == stored.data());
}

TEST_CASE("Request - param<int> - non-numeric string returns BadConversion",
          "[http][request]")
{
    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    pr.target = "/";

    std::unordered_map<std::string, std::string> params;
    params["id"] = "notanumber";

    auto req    = make_test_request({}, std::move(pr), std::move(params));
    auto result = req.param<int>("id");

    REQUIRE(!result.has_value());
    CHECK(result.error() == aevox::ParamError::BadConversion);
}

TEST_CASE("Request - param<T> - missing param returns NotFound", "[http][request]")
{
    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    pr.target = "/";
    // No params injected.

    auto req    = make_test_request({}, std::move(pr));
    auto result = req.param<int>("missing");

    REQUIRE(!result.has_value());
    CHECK(result.error() == aevox::ParamError::NotFound);
}

// =============================================================================
// Request tests — body()
// =============================================================================

TEST_CASE("Request - body() - returns correct span into owned buffer", "[http][request]")
{
    // body() returns the span stored in ParsedRequest::body, which is a span
    // into the parser's chunk_buf (not into our owned buffer). For the test we
    // use a static array as a stand-in chunk_buf.
    static const std::byte body_data[]{std::byte{'h'}, std::byte{'i'}};

    aevox::detail::ParsedRequest pr;
    pr.method = "POST";
    pr.target = "/";
    pr.body   = std::span<const std::byte>{body_data};

    auto req     = make_test_request({}, std::move(pr));
    auto body_sp = req.body();

    REQUIRE(body_sp.size() == 2);
    CHECK(static_cast<char>(body_sp[0]) == 'h');
    CHECK(static_cast<char>(body_sp[1]) == 'i');
}

// =============================================================================
// Request tests — json<T>()
// =============================================================================

TEST_CASE("Request - json<T>() - returns NotImplemented in v0.1", "[http][request]")
{
    aevox::detail::ParsedRequest pr;
    pr.method = "POST";
    pr.target = "/";

    auto req  = make_test_request({}, std::move(pr));
    auto task = req.json<int>();

    // Drive the lazy coroutine to completion synchronously.
    auto result = drive_task(std::move(task));

    REQUIRE(!result.has_value());
    CHECK(result.error() == aevox::BodyParseError::NotImplemented);
}

// =============================================================================
// Request tests — method(), path(), query()
// =============================================================================

TEST_CASE("Request - method() - GET parsed correctly", "[http][request]")
{
    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    pr.target = "/";

    auto req = make_test_request({}, std::move(pr));
    CHECK(req.method() == aevox::HttpMethod::GET);
}

TEST_CASE("Request - path() - splits target at question mark", "[http][request]")
{
    // We need the target string_view to be backed by a stable buffer.
    // Use the owned buffer for this.
    const std::string_view raw = "/users/42?sort=asc";
    auto                   buf = make_buffer(raw);

    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    // target must be a view into buf — use helper to avoid naked reinterpret_cast.
    pr.target = bytes_as_string_view(buf);

    auto req = make_test_request(std::move(buf), std::move(pr));
    CHECK(req.path() == "/users/42");
}

TEST_CASE("Request - query() - returns portion after question mark", "[http][request]")
{
    const std::string_view raw = "/users?sort=asc&page=2";
    auto                   buf = make_buffer(raw);

    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    pr.target = bytes_as_string_view(buf);

    auto req = make_test_request(std::move(buf), std::move(pr));
    CHECK(req.query() == "sort=asc&page=2");
}

TEST_CASE("Request - query() - empty when no query string present", "[http][request]")
{
    const std::string_view raw = "/users/42";
    auto                   buf = make_buffer(raw);

    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    pr.target = bytes_as_string_view(buf);

    auto req = make_test_request(std::move(buf), std::move(pr));
    CHECK(req.query().empty());
}

// =============================================================================
// Request tests — context store (set / get)
// =============================================================================

TEST_CASE("Request - context store - set and get roundtrip typed value", "[http][request]")
{
    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    pr.target = "/";

    auto req = make_test_request({}, std::move(pr));

    req.set("auth.user", std::string{"alice"});
    auto val = req.get<std::string>("auth.user");

    REQUIRE(val.has_value());
    CHECK(*val == "alice");
}

TEST_CASE("Request - context store - get returns nullopt for absent key",
          "[http][request]")
{
    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    pr.target = "/";

    auto req = make_test_request({}, std::move(pr));
    auto val = req.get<int>("missing.key");

    CHECK(!val.has_value());
}

TEST_CASE("Request - context store - get returns nullopt for type mismatch",
          "[http][request]")
{
    aevox::detail::ParsedRequest pr;
    pr.method = "GET";
    pr.target = "/";

    auto req = make_test_request({}, std::move(pr));

    // Store as int, retrieve as string — must return nullopt.
    req.set("slot", 42);
    auto val = req.get<std::string>("slot");

    CHECK(!val.has_value());
}

// =============================================================================
// Response tests — factory methods
// =============================================================================

TEST_CASE("Response - ok() sets status 200", "[http][response]")
{
    auto res = aevox::Response::ok("Hello");
    CHECK(res.status_code() == 200);
    CHECK(res.body_view() == "Hello");
}

TEST_CASE("Response - not_found() sets status 404", "[http][response]")
{
    auto res = aevox::Response::not_found();
    CHECK(res.status_code() == 404);
}

TEST_CASE("Response - bad_request() sets status 400 with body", "[http][response]")
{
    auto res = aevox::Response::bad_request("invalid input");
    CHECK(res.status_code() == 400);
    CHECK(res.body_view() == "invalid input");
}

TEST_CASE("Response - created() sets status 201", "[http][response]")
{
    auto res = aevox::Response::created("resource created");
    CHECK(res.status_code() == 201);
}

TEST_CASE("Response - unauthorized() sets status 401", "[http][response]")
{
    auto res = aevox::Response::unauthorized();
    CHECK(res.status_code() == 401);
}

TEST_CASE("Response - forbidden() sets status 403", "[http][response]")
{
    auto res = aevox::Response::forbidden();
    CHECK(res.status_code() == 403);
}

TEST_CASE("Response - json(string) sets Content-Type application/json", "[http][response]")
{
    auto res = aevox::Response::json(std::string{R"({"key":"value"})"});
    CHECK(res.status_code() == 200);
    CHECK(res.body_view() == R"({"key":"value"})");

    // Verify Content-Type header via public get_header() accessor.
    auto ct = res.get_header("Content-Type");
    REQUIRE(ct.has_value());
    CHECK(*ct == "application/json");
}

TEST_CASE("Response - json<T>() produces sentinel body in v0.1", "[http][response]")
{
    struct MyType
    {
        int x;
    };
    auto res = aevox::Response::json(MyType{42});

    CHECK(res.status_code() == 200);
    CHECK(res.body_view() == R"({"error":"not_implemented"})");

    // Verify Content-Type header via public get_header() accessor.
    auto ct = res.get_header("Content-Type");
    REQUIRE(ct.has_value());
    CHECK(*ct == "application/json");
}

// =============================================================================
// Response tests — fluent builder
// =============================================================================

TEST_CASE("Response - content_type() fluent lvalue overload modifies in place",
          "[http][response]")
{
    auto res = aevox::Response::ok("body");
    // The lvalue overload mutates in place and returns *this for chaining.
    // We intentionally discard the reference here — the mutation already happened.
    (void)res.content_type("text/html");

    auto ct = res.get_header("Content-Type");
    REQUIRE(ct.has_value());
    CHECK(*ct == "text/html");
}

TEST_CASE("Response - content_type() fluent rvalue overload chains on temporary",
          "[http][response]")
{
    auto res = aevox::Response::ok("body").content_type("text/html");

    CHECK(res.status_code() == 200);
    CHECK(res.body_view() == "body");

    auto ct = res.get_header("Content-Type");
    REQUIRE(ct.has_value());
    CHECK(*ct == "text/html");
}

TEST_CASE("Response - header() fluent lvalue overload sets header", "[http][response]")
{
    auto res = aevox::Response::ok();
    // The lvalue overload mutates in place and returns *this for chaining.
    // We intentionally discard the reference here — the mutation already happened.
    (void)res.header("X-Request-Id", "abc-123");

    auto val = res.get_header("X-Request-Id");
    REQUIRE(val.has_value());
    CHECK(*val == "abc-123");
}

// =============================================================================
// Response tests — move semantics
// =============================================================================

TEST_CASE("Response - move semantics - moved-from Response is valid but empty",
          "[http][response]")
{
    auto original = aevox::Response::ok("hello world");
    REQUIRE(original.status_code() == 200);
    REQUIRE(original.body_view() == "hello world");

    // Move construct.
    auto moved = std::move(original);

    // Moved-into has the original values.
    CHECK(moved.status_code() == 200);
    CHECK(moved.body_view() == "hello world");

    // Moved-from is valid but empty (status_code() == 0, body empty).
    CHECK(original.status_code() == 0);
    CHECK(original.body_view().empty());
}

// =============================================================================
// Response tests — stream()
// =============================================================================

TEST_CASE("Response - stream() returns status 200 with given content type",
          "[http][response]")
{
    auto res = aevox::Response::stream("text/event-stream");

    CHECK(res.status_code() == 200);
    CHECK(res.body_view().empty());

    auto ct = res.get_header("Content-Type");
    REQUIRE(ct.has_value());
    CHECK(*ct == "text/event-stream");
}
