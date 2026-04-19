// AEV-004: Router path matching, parameter extraction, and dispatch — unit tests.
// ADD ref: Tasks/architecture/AEV-004-arch.md §8.1
//
// Tests drive Router::dispatch() synchronously via drive_task<T>.
// Request objects are built via internal PIMPL helpers (request_impl.hpp).
// No I/O, no Executor, no Asio.

#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/router.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <barrier>
#include <coroutine>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "http/request_impl.hpp"
#include "http/http_parser.hpp"

// =============================================================================
// Test helpers
// =============================================================================

static std::vector<std::byte> make_buffer(std::string_view s)
{
    std::vector<std::byte> buf(s.size());
    std::memcpy(buf.data(), s.data(), s.size());
    return buf;
}

/// Builds a minimal Request for the given method and path.
static aevox::Request make_test_request(aevox::HttpMethod method, std::string_view path)
{
    aevox::detail::ParsedRequest pr;
    pr.method     = aevox::to_string(method);
    pr.target     = path;
    pr.keep_alive = true;

    // Buffer must own the strings that ParsedRequest views point into.
    std::string raw = std::string{pr.method} + " " + std::string{path} + " HTTP/1.1\r\n\r\n";
    auto buf = make_buffer(raw);

    // Build via the internal factory. Recalculate views into buf.
    aevox::detail::ParsedRequest pr2;
    // Cast from std::byte* to const char* is defined behaviour per [basic.lval] §11.
    pr2.method     = std::string_view{reinterpret_cast<const char*>(buf.data()), pr.method.size()};
    pr2.target     = std::string_view{
        reinterpret_cast<const char*>(buf.data()) + pr.method.size() + 1,
        path.size()};
    pr2.keep_alive = true;

    return aevox::make_request_from_impl(std::move(buf), std::move(pr2));
}

/// Drives a lazy Task<T> to completion synchronously (no event loop).
template <typename T>
static T drive_task(aevox::Task<T> task)
{
    auto inner = task.await_suspend(std::noop_coroutine());
    inner.resume();
    return task.await_resume();
}

// =============================================================================
// Static path matching
// =============================================================================

TEST_CASE("AEV-004: Router — static path matching", "[router]")
{
    SECTION("exact match returns 200")
    {
        aevox::Router r;
        r.get("/hello", [](aevox::Request&) {
            return aevox::Response::ok("hello");
        });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/hello");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 200);
        REQUIRE(resp.body_view() == "hello");
    }

    SECTION("root path / matches")
    {
        aevox::Router r;
        r.get("/", [](aevox::Request&) { return aevox::Response::ok("root"); });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 200);
        REQUIRE(resp.body_view() == "root");
    }

    SECTION("unregistered path returns 404")
    {
        aevox::Router r;
        r.get("/hello", [](aevox::Request&) { return aevox::Response::ok("x"); });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/world");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 404);
    }
}

// =============================================================================
// Named parameter extraction
// =============================================================================

TEST_CASE("AEV-004: Router — named parameter extraction", "[router]")
{
    SECTION("string param is extracted")
    {
        aevox::Router r;
        r.get("/users/{name}", [](aevox::Request& /*req*/, std::string name) {
            return aevox::Response::ok(name);
        });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/users/alice");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 200);
        REQUIRE(resp.body_view() == "alice");
    }

    SECTION("int param is extracted and converted")
    {
        aevox::Router r;
        r.get("/items/{id:int}", [](aevox::Request&, int id) {
            return aevox::Response::ok(std::to_string(id));
        });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/items/42");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 200);
        REQUIRE(resp.body_view() == "42");
    }

    SECTION("bad int param returns 400")
    {
        aevox::Router r;
        r.get("/items/{id:int}", [](aevox::Request&, int id) {
            return aevox::Response::ok(std::to_string(id));
        });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/items/notanumber");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 400);
    }

    SECTION("two params extracted left-to-right")
    {
        aevox::Router r;
        r.get("/a/{x}/b/{y}", [](aevox::Request&, std::string x, std::string y) {
            return aevox::Response::ok(x + "+" + y);
        });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/a/foo/b/bar");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 200);
        REQUIRE(resp.body_view() == "foo+bar");
    }
}

// =============================================================================
// Wildcard capture
// =============================================================================

TEST_CASE("AEV-004: Router — wildcard capture", "[router]")
{
    SECTION("wildcard captures tail")
    {
        aevox::Router r;
        r.get("/files/{path...}", [](aevox::Request&, std::string p) {
            return aevox::Response::ok(p);
        });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/files/a/b/c");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 200);
        REQUIRE(resp.body_view() == "a/b/c");
    }

    SECTION("wildcard captures single segment")
    {
        aevox::Router r;
        r.get("/files/{path...}", [](aevox::Request&, std::string p) {
            return aevox::Response::ok(p);
        });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/files/readme.txt");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 200);
        REQUIRE(resp.body_view() == "readme.txt");
    }

    SECTION("no match before wildcard prefix returns 404")
    {
        aevox::Router r;
        r.get("/files/{path...}", [](aevox::Request&, std::string p) {
            return aevox::Response::ok(p);
        });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/other/readme.txt");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 404);
    }
}

// =============================================================================
// Handler invocation — all supported arities and return types
// =============================================================================

TEST_CASE("AEV-004: Router — handler invocation", "[router]")
{
    SECTION("sync arity-0 handler")
    {
        aevox::Router r;
        r.get("/sync", [](aevox::Request&) { return aevox::Response::ok("sync"); });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/sync");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 200);
        REQUIRE(resp.body_view() == "sync");
    }

    SECTION("async arity-0 handler")
    {
        aevox::Router r;
        r.get("/async", [](aevox::Request&) -> aevox::Task<aevox::Response> {
            co_return aevox::Response::ok("async");
        });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/async");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 200);
        REQUIRE(resp.body_view() == "async");
    }

    SECTION("sync arity-1 string handler")
    {
        aevox::Router r;
        r.get("/echo/{name}", [](aevox::Request&, std::string name) {
            return aevox::Response::ok(name);
        });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/echo/world");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 200);
        REQUIRE(resp.body_view() == "world");
    }

    SECTION("sync arity-2 (string, int) handler")
    {
        aevox::Router r;
        r.get("/repo/{owner}/{num:int}", [](aevox::Request&, std::string owner, int num) {
            return aevox::Response::ok(owner + "/" + std::to_string(num));
        });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/repo/alice/7");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 200);
        REQUIRE(resp.body_view() == "alice/7");
    }

    SECTION("POST handler on POST method")
    {
        aevox::Router r;
        r.post("/submit", [](aevox::Request&) { return aevox::Response::created("ok"); });

        auto req  = make_test_request(aevox::HttpMethod::POST, "/submit");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 201);
    }
}

// =============================================================================
// 405 Method Not Allowed
// =============================================================================

TEST_CASE("AEV-004: Router — method not allowed", "[router]")
{
    SECTION("GET-only route returns 405 for POST with Allow header")
    {
        aevox::Router r;
        r.get("/resource", [](aevox::Request&) { return aevox::Response::ok("x"); });

        auto req  = make_test_request(aevox::HttpMethod::POST, "/resource");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 405);
        auto allow = resp.get_header("Allow");
        REQUIRE(allow.has_value());
        REQUIRE(allow->find("GET") != std::string_view::npos);
    }
}

// =============================================================================
// Concurrent dispatch thread safety
// =============================================================================

TEST_CASE("AEV-004: Router — concurrent dispatch thread safety", "[router]")
{
    SECTION("1000 concurrent dispatches on read-only trie")
    {
        aevox::Router r;
        r.get("/ping", [](aevox::Request&) { return aevox::Response::ok("pong"); });

        constexpr int kThreads = 8;
        constexpr int kIter    = 125; // 8 * 125 = 1000 total

        std::atomic<int>     ok_count{0};
        std::barrier<>       start_gate{kThreads};
        std::vector<std::thread> threads;
        threads.reserve(kThreads);

        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&] {
                start_gate.arrive_and_wait(); // all threads start simultaneously
                for (int i = 0; i < kIter; ++i) {
                    auto req  = make_test_request(aevox::HttpMethod::GET, "/ping");
                    auto resp = drive_task(r.dispatch(req));
                    if (resp.status_code() == 200)
                        ok_count.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& th : threads)
            th.join();

        REQUIRE(ok_count.load() == kThreads * kIter);
    }
}

// =============================================================================
// Group prefix
// =============================================================================

TEST_CASE("AEV-004: Router — group prefix", "[router]")
{
    SECTION("group routes are accessible via prefixed path")
    {
        aevox::Router r;
        auto api = r.group("/api/v1");
        api.get("/users", [](aevox::Request&) { return aevox::Response::ok("users"); });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/api/v1/users");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 200);
        REQUIRE(resp.body_view() == "users");
    }

    SECTION("group route not accessible without prefix")
    {
        aevox::Router r;
        auto api = r.group("/api/v1");
        api.get("/users", [](aevox::Request&) { return aevox::Response::ok("users"); });

        auto req  = make_test_request(aevox::HttpMethod::GET, "/users");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 404);
    }
}

// =============================================================================
// Match priority: static > param > wildcard
// =============================================================================

TEST_CASE("AEV-004: Router — match priority", "[router]")
{
    SECTION("static segment takes priority over param at same level")
    {
        aevox::Router r;
        r.get("/users/me", [](aevox::Request&) { return aevox::Response::ok("me"); });
        r.get("/users/{id}", [](aevox::Request&, std::string id) {
            return aevox::Response::ok("id:" + id);
        });

        auto req_me  = make_test_request(aevox::HttpMethod::GET, "/users/me");
        auto resp_me = drive_task(r.dispatch(req_me));
        REQUIRE(resp_me.status_code() == 200);
        REQUIRE(resp_me.body_view() == "me");

        auto req_id  = make_test_request(aevox::HttpMethod::GET, "/users/42");
        auto resp_id = drive_task(r.dispatch(req_id));
        REQUIRE(resp_id.status_code() == 200);
        REQUIRE(resp_id.body_view() == "id:42");
    }

    SECTION("param takes priority over wildcard at same level")
    {
        aevox::Router r;
        r.get("/a/{id}", [](aevox::Request&, std::string id) {
            return aevox::Response::ok("param:" + id);
        });
        r.get("/a/{rest...}", [](aevox::Request&, std::string rest) {
            return aevox::Response::ok("wild:" + rest);
        });

        // Single segment — param match preferred
        auto req  = make_test_request(aevox::HttpMethod::GET, "/a/hello");
        auto resp = drive_task(r.dispatch(req));
        REQUIRE(resp.status_code() == 200);
        REQUIRE(resp.body_view() == "param:hello");
    }
}
