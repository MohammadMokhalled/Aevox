// AEV-024: auth guard middleware pattern (drive_task, synchronous)
// ADD ref: Tasks/architecture/AEV-024-arch.md § Test Architecture
//
// Verifies middleware can inspect request state and short-circuit with 401.
// Reclassified from integration/ (no Asio io_context, no network I/O).

#include <aevox/app.hpp>
#include <aevox/middleware.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "http/request_impl.hpp"
#include "router/router_impl.hpp"

namespace {

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

} // namespace

TEST_CASE("Middleware: auth guard middleware", "[middleware][unit]")
{
    SECTION("middleware can check request and short-circuit")
    {
        auto auth_mw =
            [](aevox::Request&                                                        req,
               std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
            -> aevox::Task<aevox::Response> {
            auto auth = req.get<std::string>("is_authenticated");
            if (!auth || *auth != "yes") {
                co_return aevox::Response::unauthorized("Not authenticated");
            }
            co_return co_await next(req);
        };

        aevox::Router router;
        router.get("/protected",
                   [](aevox::Request& /*req*/) { return aevox::Response::ok("Secret!"); });

        auto handler = [&router](aevox::Request& req) -> aevox::Task<aevox::Response> {
            co_return co_await router.dispatch(req);
        };

        // Request without auth should fail
        auto req1 = make_test_request(aevox::HttpMethod::GET, "/protected");
        auto res1 = drive_task(auth_mw(
            req1, std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)>(handler)));
        REQUIRE(res1.status_code() == 401);

        // Request with auth should succeed
        auto req2 = make_test_request(aevox::HttpMethod::GET, "/protected");
        req2.set("is_authenticated", std::string("yes"));
        auto res2 = drive_task(auth_mw(
            req2, std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)>(handler)));
        REQUIRE(res2.status_code() == 200);
        REQUIRE(res2.body_view() == "Secret!");
    }
}
