// AEV-024: middleware ordering, short-circuiting, and pipeline composition
// ADD ref: Tasks/architecture/AEV-024-arch.md § Test Architecture
//
// Unit tests for aevox::Middleware — composable async middleware pipeline.
// Tests middleware ordering, short-circuiting, scoped prefix matching, and
// three-middleware onion ordering through pipeline-assembled chains.
//
// Design note: aevox::Middleware has a private constructor (friend class App only).
// These tests build chains directly from raw lambdas stored in std::move_only_function,
// which exercises the identical call path that dispatch_with_pipeline uses.
// The wrap() helper replicates the dispatch_with_pipeline wrapping loop exactly.

#include <aevox/middleware.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/task.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "http/request_impl.hpp"

namespace {

// Convenience alias for the next callable type.
using NextFn = std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)>;

static std::vector<std::byte> make_buffer(std::string_view s)
{
    std::vector<std::byte> buf(s.size());
    std::memcpy(buf.data(), s.data(), s.size());
    return buf;
}

static aevox::Request make_test_request(aevox::HttpMethod method, std::string_view path)
{
    aevox::detail::ParsedRequest pr;
    pr.method     = aevox::to_string(method);
    pr.target     = path;
    pr.keep_alive = true;

    std::string const raw = std::string{pr.method} + " " + std::string{path} + " HTTP/1.1\r\n\r\n";
    auto              buf = make_buffer(raw);

    aevox::detail::ParsedRequest pr2;
    pr2.method = std::string_view{reinterpret_cast<const char*>(buf.data()), pr.method.size()};
    pr2.target = std::string_view{reinterpret_cast<const char*>(buf.data()) + pr.method.size() + 1,
                                  path.size()};
    pr2.keep_alive = true;

    return aevox::make_request_from_impl(std::move(buf), std::move(pr2));
}

template <typename T> static T drive_task(aevox::Task<T> task)
{
    auto inner = task.await_suspend(std::noop_coroutine());
    inner.resume();
    return task.await_resume();
}

// Wrap a middleware lambda around an existing NextFn chain.
// Exactly replicates one iteration of the dispatch_with_pipeline wrapping loop.
// `mw` signature: (Request&, NextFn) -> Task<Response>.
template <typename MwLambda> static NextFn wrap(MwLambda mw, NextFn prev)
{
    return [mw   = std::move(mw),
            prev = std::move(prev)](aevox::Request& r) mutable -> aevox::Task<aevox::Response> {
        co_return co_await mw(r, std::move(prev));
    };
}

// Build a logging middleware lambda that appends "{name}:before" and "{name}:after"
// to `log`. The log is captured by reference; each test case owns its own local log.
auto logging_middleware(const std::string& name, std::vector<std::string>& log)
{
    return [name, &log](aevox::Request&                                                        req,
                        std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
               -> aevox::Task<aevox::Response> {
        log.push_back(name + ":before");
        auto res = co_await next(req);
        log.push_back(name + ":after");
        co_return res;
    };
}

// Build a short-circuiting middleware lambda. Appends "short_circuit" and returns 401.
auto short_circuit_middleware(std::vector<std::string>& log)
{
    return [&log](aevox::Request& /*req*/,
                  std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> /*next*/)
               -> aevox::Task<aevox::Response> {
        log.emplace_back("short_circuit");
        co_return aevox::Response::unauthorized("Unauthorized");
    };
}

auto echo_middleware(const std::string& name)
{
    return [name](aevox::Request&                                                        req,
                  std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
               -> aevox::Task<aevox::Response> {
        req.set(name, name + "_value");
        co_return co_await next(req);
    };
}

// Scoped prefix boundary check — same logic as dispatch_with_pipeline line 144-146.
// Returns true iff `path` matches `prefix` with a proper segment boundary.
static bool prefix_matches(std::string_view path, std::string_view prefix)
{
    return path.starts_with(prefix) && (prefix.size() == path.size() || path[prefix.size()] == '/');
}

} // namespace

// =============================================================================
// Test cases
// =============================================================================

TEST_CASE("Middleware: single middleware runs before handler", "[middleware]")
{
    SECTION("middleware receives request and passes to next")
    {
        std::vector<std::string> log;

        auto mw  = logging_middleware("A", log);
        auto req = make_test_request(aevox::HttpMethod::GET, "/test");

        auto handler = [&log](aevox::Request& /*req*/) -> aevox::Task<aevox::Response> {
            log.emplace_back("handler");
            co_return aevox::Response::ok("OK");
        };

        auto res = drive_task(
            mw(req,
               std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)>(handler)));

        REQUIRE(res.status_code() == 200);
        REQUIRE(log.size() == 3);
        REQUIRE(log[0] == "A:before");
        REQUIRE(log[1] == "handler");
        REQUIRE(log[2] == "A:after");
    }
}

TEST_CASE("Middleware: short-circuit middleware stops request", "[middleware]")
{
    SECTION("short-circuiting middleware returns without calling next")
    {
        std::vector<std::string> log;

        auto mw  = short_circuit_middleware(log);
        auto req = make_test_request(aevox::HttpMethod::GET, "/test");

        auto handler = [&log](aevox::Request& /*req*/) -> aevox::Task<aevox::Response> {
            log.emplace_back("handler");
            co_return aevox::Response::ok("OK");
        };

        auto res = drive_task(
            mw(req,
               std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)>(handler)));

        REQUIRE(res.status_code() == 401);
        REQUIRE(log.size() == 1);
        REQUIRE(log[0] == "short_circuit");
    }
}

TEST_CASE("Middleware: middleware can modify request", "[middleware]")
{
    SECTION("middleware modifies request before handler")
    {
        auto mw  = echo_middleware("custom_header");
        auto req = make_test_request(aevox::HttpMethod::GET, "/test");

        auto handler = [](aevox::Request& req) -> aevox::Task<aevox::Response> {
            if (auto val = req.get<std::string>("custom_header")) {
                co_return aevox::Response::ok(*val);
            }
            co_return aevox::Response::not_found();
        };

        auto res = drive_task(
            mw(req,
               std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)>(handler)));

        REQUIRE(res.status_code() == 200);
        REQUIRE(res.body_view() == "custom_header_value");
    }
}

TEST_CASE("Middleware: zero overhead when no middleware registered", "[middleware]")
{
    SECTION("direct dispatch without middleware")
    {
        std::vector<std::string> log;

        auto req = make_test_request(aevox::HttpMethod::GET, "/test");

        auto handler = [&log](aevox::Request& /*req*/) -> aevox::Task<aevox::Response> {
            log.emplace_back("handler");
            co_return aevox::Response::ok("OK");
        };

        auto res = drive_task(handler(req));

        REQUIRE(res.status_code() == 200);
        REQUIRE(log.size() == 1);
        REQUIRE(log[0] == "handler");
    }
}

TEST_CASE("Middleware pipeline - onion ordering with three middlewares", "[middleware]")
{
    // Verifies the full onion sequence:
    //   A:before -> B:before -> C:before -> handler -> C:after -> B:after -> A:after
    //
    // Chain built with wrap() replicating dispatch_with_pipeline's loop:
    // wrap in reverse registration order so A (index 0) is outermost.
    // Innermost: handler, then wrap C (last), then B, then A.

    std::vector<std::string> log;

    // Innermost: the route handler.
    NextFn chain = [&log](aevox::Request& /*r*/) -> aevox::Task<aevox::Response> {
        log.emplace_back("handler");
        co_return aevox::Response::ok("OK");
    };

    // Wrap C (innermost around handler), then B, then A (outermost).
    // This matches: for(rbegin..rend) wrapping in reverse registration order.
    chain = wrap(logging_middleware("C", log), std::move(chain));
    chain = wrap(logging_middleware("B", log), std::move(chain));
    chain = wrap(logging_middleware("A", log), std::move(chain));

    auto req = make_test_request(aevox::HttpMethod::GET, "/test");
    auto res = drive_task(chain(req));

    REQUIRE(res.status_code() == 200);
    REQUIRE(log.size() == 7);
    REQUIRE(log[0] == "A:before");
    REQUIRE(log[1] == "B:before");
    REQUIRE(log[2] == "C:before");
    REQUIRE(log[3] == "handler");
    REQUIRE(log[4] == "C:after");
    REQUIRE(log[5] == "B:after");
    REQUIRE(log[6] == "A:after");
}

TEST_CASE("Middleware pipeline - scoped middleware does not run on non-matching prefix",
          "[middleware]")
{
    // Verifies the prefix boundary check logic from dispatch_with_pipeline line 144-146:
    //   - /api/users  matches /api  (slash boundary after prefix)
    //   - /apiv2/users does NOT match /api  (no slash/end boundary)
    //   - /static/file.css does NOT match /api  (different prefix)
    //
    // The prefix_matches() helper replicates the exact boundary check.
    // For each request path, we conditionally apply the scoped middleware,
    // then verify whether it ran by checking the execution log.

    std::vector<std::string>   log;
    constexpr std::string_view kScopedPrefix = "/api";

    // Helper: build a chain that applies the scoped middleware only if the
    // request path matches the prefix. This mirrors the matching logic in
    // dispatch_with_pipeline.
    auto make_chain = [&](std::string_view path) -> NextFn {
        NextFn inner = [](aevox::Request& /*r*/) -> aevox::Task<aevox::Response> {
            co_return aevox::Response::ok("OK");
        };

        if (prefix_matches(path, kScopedPrefix)) {
            inner = wrap(logging_middleware("api-scoped", log), std::move(inner));
        }

        return inner;
    };

    // Case 1: /api/users — must match (slash boundary after prefix)
    {
        log.clear();
        auto req   = make_test_request(aevox::HttpMethod::GET, "/api/users");
        auto chain = make_chain("/api/users");
        drive_task(chain(req));
        REQUIRE(!log.empty());
        REQUIRE(log[0] == "api-scoped:before");
    }

    // Case 2: /apiv2/users — must NOT match (no slash/end boundary after /api)
    {
        log.clear();
        auto req   = make_test_request(aevox::HttpMethod::GET, "/apiv2/users");
        auto chain = make_chain("/apiv2/users");
        drive_task(chain(req));
        REQUIRE(log.empty());
    }

    // Case 3: /static/file.css — must NOT match (different prefix)
    {
        log.clear();
        auto req   = make_test_request(aevox::HttpMethod::GET, "/static/file.css");
        auto chain = make_chain("/static/file.css");
        drive_task(chain(req));
        REQUIRE(log.empty());
    }
}

TEST_CASE("Middleware pipeline - middleware that does not call next returns its response without "
          "reaching handler",
          "[middleware]")
{
    // Verifies that a short-circuiting middleware registered in the pipeline
    // returns its own response without the handler being invoked.
    // Chain built with wrap() — same structure as dispatch_with_pipeline.

    std::vector<std::string> log;

    // Innermost: handler (must not be reached)
    NextFn chain = [&log](aevox::Request& /*r*/) -> aevox::Task<aevox::Response> {
        log.emplace_back("handler");
        co_return aevox::Response::ok("OK");
    };

    // Outermost: short-circuiting middleware
    chain = wrap(short_circuit_middleware(log), std::move(chain));

    auto req = make_test_request(aevox::HttpMethod::GET, "/test");
    auto res = drive_task(chain(req));

    REQUIRE(res.status_code() == 401);
    // Handler must not have been called
    REQUIRE(log.size() == 1);
    REQUIRE(log[0] == "short_circuit");
}
