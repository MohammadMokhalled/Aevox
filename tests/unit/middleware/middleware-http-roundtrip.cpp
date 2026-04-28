// AEV-024: middleware with router dispatch (drive_task, synchronous)
// ADD ref: Tasks/architecture/AEV-024-arch.md § Test Architecture
//
// Verifies middleware can modify responses when composed with a real Router.
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

} // namespace

TEST_CASE("Middleware: HTTP roundtrip with middleware adding response header", "[middleware][unit]")
{
    auto mw = [](aevox::Request&                                                        req,
                 std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
        -> aevox::Task<aevox::Response> {
        auto res = co_await next(req);
        res      = std::move(res).header("X-Middleware-Added", "true");
        co_return res;
    };

    aevox::Router router;
    router.get("/hello",
               [](aevox::Request& /*req*/) { return aevox::Response::ok("Hello, World!"); });

    auto req = make_test_request(aevox::HttpMethod::GET, "/hello");

    auto handler = [&router](aevox::Request& req) -> aevox::Task<aevox::Response> {
        co_return co_await router.dispatch(req);
    };

    auto res = drive_task(
        mw(req, std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)>(handler)));

    REQUIRE(res.status_code() == 200);
    REQUIRE(res.body_view() == "Hello, World!");
    auto header = res.get_header("X-Middleware-Added");
    REQUIRE(header.has_value());
    REQUIRE(*header == "true"); // NOLINT(bugprone-unchecked-optional-access)
}
