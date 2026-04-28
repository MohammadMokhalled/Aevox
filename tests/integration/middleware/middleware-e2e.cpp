// AEV-024: middleware pipeline end-to-end integration tests
// ADD ref: Tasks/architecture/AEV-024-arch.md § Test Architecture
//
// Uses real aevox::App on an ephemeral loopback port.
// Client side uses raw Asio sockets. No mocks. No synchronous coroutine driver.

#include <aevox/app.hpp>
#include <aevox/middleware.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <latch>
#include <string>
#include <thread>

using namespace std::chrono_literals;

// =============================================================================
// Helpers — identical pattern to tests/integration/router/router-e2e.cpp
// =============================================================================

namespace {

/// Returns an available ephemeral port.
std::uint16_t free_port()
{
    asio::io_context              ioc;
    asio::ip::tcp::acceptor const a{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return a.local_endpoint().port();
}

/// Sends `request_str` over a loopback TCP connection and returns the full response.
std::string http_roundtrip(std::uint16_t port, std::string_view request_str)
{
    asio::io_context      ioc;
    asio::ip::tcp::socket s{ioc};
    asio::error_code      ec;
    auto const            ep = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port};
    s.connect(ep, ec); // NOLINT(bugprone-unused-return-value)
    if (ec)
        return {};
    asio::write(s, asio::buffer(request_str.data(), request_str.size()),
                ec); // NOLINT(bugprone-unused-return-value)
    if (ec)
        return {};

    std::string     response;
    asio::streambuf buf;
    asio::read(s, buf, asio::transfer_at_least(1), ec); // NOLINT(bugprone-unused-return-value)
    response = std::string{asio::buffers_begin(buf.data()), asio::buffers_end(buf.data())};
    return response;
}

/// Minimal HTTP/1.0 GET (Connection: close so server closes after reply).
std::string http_get(std::uint16_t port, std::string_view path)
{
    return http_roundtrip(port, std::format("GET {} HTTP/1.0\r\nHost: localhost\r\n\r\n", path));
}

} // namespace

// =============================================================================
// TestServer — starts App in a background thread, stops on destruction.
// configure_fn is called against app BEFORE listen() to satisfy the Router
// thread-safety contract: all registration completes before any dispatch call.
// =============================================================================

struct TestServer
{
    explicit TestServer(std::uint16_t p, auto configure_fn) : port{p}
    {
        configure_fn(app);
        thread = std::jthread{[this] {
            ready.count_down();
            app.listen(port);
        }};
        ready.wait();
        std::this_thread::sleep_for(20ms); // let the executor's accept loop bind
    }

    ~TestServer()
    {
        app.stop();
    }

    TestServer(const TestServer&)            = delete;
    TestServer& operator=(const TestServer&) = delete;
    TestServer(TestServer&&)                 = delete;
    TestServer& operator=(TestServer&&)      = delete;

    aevox::App    app{aevox::AppConfig{.executor = {.thread_count = 2, .drain_timeout = 2s}}};
    std::uint16_t port;
    std::latch    ready{1};
    std::jthread  thread;
};

// =============================================================================
// Integration tests
// =============================================================================

TEST_CASE("global middleware adds response header", "[middleware][integration]")
{
    const auto       port = free_port();
    TestServer const server{
        port, [](aevox::App& app) {
            app.use([](aevox::Request&                                                        req,
                       std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
                        -> aevox::Task<aevox::Response> {
                auto res = co_await next(req);
                res      = std::move(res).header("X-Global", "true");
                co_return res;
            });
            app.get("/hello", [](aevox::Request&) { return aevox::Response::ok("Hello"); });
        }};

    const auto resp = http_get(port, "/hello");
    REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
    REQUIRE(resp.find("X-Global: true") != std::string::npos);
}

TEST_CASE("scoped middleware runs on matching prefix, not on non-matching path",
          "[middleware][integration]")
{
    const auto       port = free_port();
    TestServer const server{
        port, [](aevox::App& app) {
            app.use("/api",
                    [](aevox::Request&                                                        req,
                       std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
                        -> aevox::Task<aevox::Response> {
                        auto res = co_await next(req);
                        res      = std::move(res).header("X-Scoped", "api");
                        co_return res;
                    });
            app.get("/api/hello", [](aevox::Request&) { return aevox::Response::ok("api"); });
            app.get("/other/path", [](aevox::Request&) { return aevox::Response::ok("other"); });
        }};

    // Matching prefix: header must be present
    const auto resp_api = http_get(port, "/api/hello");
    REQUIRE(resp_api.find("HTTP/1.1 200") != std::string::npos);
    REQUIRE(resp_api.find("X-Scoped: api") != std::string::npos);

    // Non-matching path: header must be absent
    const auto resp_other = http_get(port, "/other/path");
    REQUIRE(resp_other.find("HTTP/1.1 200") != std::string::npos);
    REQUIRE(resp_other.find("X-Scoped") == std::string::npos);
}

TEST_CASE("middleware can short-circuit and return 401", "[middleware][integration]")
{
    const auto       port = free_port();
    TestServer const server{
        port, [](aevox::App& app) {
            app.use(
                [](aevox::Request& /*req*/,
                   std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> /*next*/)
                    -> aevox::Task<aevox::Response> {
                    co_return aevox::Response::unauthorized("Unauthorized");
                });
            app.get("/secret", [](aevox::Request&) { return aevox::Response::ok("Secret"); });
        }};

    const auto resp = http_get(port, "/secret");
    REQUIRE(resp.find("HTTP/1.1 401") != std::string::npos);
}

TEST_CASE("global runs before scoped - execution order", "[middleware][integration]")
{
    // This test verifies that global middleware executes before scoped middleware
    // by using an ordered append to an X-Order response header.
    // Global appends "global"; scoped on /api appends "scoped".
    // The resulting header value must be "global,scoped" — proving global is outermost.

    const auto       port = free_port();
    TestServer const server{
        port, [](aevox::App& app) {
            // Global middleware: runs outermost (first pre, last post)
            app.use([](aevox::Request&                                                        req,
                       std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
                        -> aevox::Task<aevox::Response> {
                auto              res      = co_await next(req);
                auto              existing = res.get_header("X-Order");
                std::string const val = existing ? std::string(*existing) + ",global" : "global";
                res                   = std::move(res).header("X-Order", val);
                co_return res;
            });

            // Scoped middleware on /api: runs inner (after global pre, before global post)
            app.use("/api",
                    [](aevox::Request&                                                        req,
                       std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
                        -> aevox::Task<aevox::Response> {
                        auto              res      = co_await next(req);
                        auto              existing = res.get_header("X-Order");
                        std::string const val =
                            existing ? std::string(*existing) + ",scoped" : "scoped";
                        res = std::move(res).header("X-Order", val);
                        co_return res;
                    });

            app.get("/api/test", [](aevox::Request&) { return aevox::Response::ok("test"); });
        }};

    const auto resp = http_get(port, "/api/test");
    REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);

    // Both middlewares append in their post-handler phase.
    // Execution: global-pre -> scoped-pre -> handler -> scoped-post -> global-post
    // scoped-post runs first: sets X-Order = "scoped"
    // global-post runs second: appends to get X-Order = "scoped,global"
    // This proves global is outermost (last post-handler), scoped is inner (first post-handler).
    REQUIRE(resp.find("X-Order: scoped,global") != std::string::npos);
}
