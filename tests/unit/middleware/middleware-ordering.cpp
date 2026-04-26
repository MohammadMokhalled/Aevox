// tests/unit/middleware/middleware-ordering.cpp
//
// Unit tests for aevox::Middleware — composable async middleware pipeline.
// Tests middleware ordering, short-circuiting, and composition.

#include <aevox/middleware.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/task.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

#include "http/request_impl.hpp"

namespace {

std::vector<std::string> execution_log;

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

    std::string raw = std::string{pr.method} + " " + std::string{path} + " HTTP/1.1\r\n\r\n";
    auto        buf = make_buffer(raw);

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

auto logging_middleware(const std::string& name)
{
    return [name](aevox::Request&                                                        req,
                  std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
               -> aevox::Task<aevox::Response> {
        execution_log.push_back(name + ":before");
        auto res = co_await next(req);
        execution_log.push_back(name + ":after");
        co_return res;
    };
}

auto short_circuit_middleware()
{
    return [](aevox::Request& /*req*/,
              std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> /*next*/)
               -> aevox::Task<aevox::Response> {
        execution_log.push_back("short_circuit");
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

} // namespace

TEST_CASE("Middleware: single middleware runs before handler", "[middleware]")
{
    SECTION("middleware receives request and passes to next")
    {
        execution_log.clear();

        auto mw  = logging_middleware("A");
        auto req = make_test_request(aevox::HttpMethod::GET, "/test");

        auto handler = [](aevox::Request& /*req*/) -> aevox::Task<aevox::Response> {
            execution_log.push_back("handler");
            co_return aevox::Response::ok("OK");
        };

        auto res = drive_task(
            mw(req,
               std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)>(handler)));

        REQUIRE(res.status_code() == 200);
        REQUIRE(execution_log.size() == 3);
        REQUIRE(execution_log[0] == "A:before");
        REQUIRE(execution_log[1] == "handler");
        REQUIRE(execution_log[2] == "A:after");
    }
}

TEST_CASE("Middleware: short-circuit middleware stops request", "[middleware]")
{
    SECTION("short-circuiting middleware returns without calling next")
    {
        execution_log.clear();

        auto mw  = short_circuit_middleware();
        auto req = make_test_request(aevox::HttpMethod::GET, "/test");

        auto handler = [](aevox::Request& /*req*/) -> aevox::Task<aevox::Response> {
            execution_log.push_back("handler");
            co_return aevox::Response::ok("OK");
        };

        auto res = drive_task(
            mw(req,
               std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)>(handler)));

        REQUIRE(res.status_code() == 401);
        REQUIRE(execution_log.size() == 1);
        REQUIRE(execution_log[0] == "short_circuit");
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
        execution_log.clear();

        auto req = make_test_request(aevox::HttpMethod::GET, "/test");

        auto handler = [](aevox::Request& /*req*/) -> aevox::Task<aevox::Response> {
            execution_log.push_back("handler");
            co_return aevox::Response::ok("OK");
        };

        auto res = drive_task(handler(req));

        REQUIRE(res.status_code() == 200);
        REQUIRE(execution_log.size() == 1);
        REQUIRE(execution_log[0] == "handler");
    }
}
