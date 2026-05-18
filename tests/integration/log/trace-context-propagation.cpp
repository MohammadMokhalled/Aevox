// trace-context-propagation.cpp: verify req.trace_context() returns forwarding string_view
// ADD ref: Tasks/architecture/AEV-012-arch.md §8.2

#include <aevox/app.hpp>
#include <aevox/middleware/logger.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {

std::uint16_t free_port()
{
    asio::io_context              ioc;
    asio::ip::tcp::acceptor const a{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return a.local_endpoint().port();
}

bool can_connect(std::uint16_t port)
{
    asio::io_context      ioc;
    asio::ip::tcp::socket socket{ioc};
    asio::error_code      ec;
    const auto            ep = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port};
    ec                       = socket.connect(ep, ec);
    return !ec;
}

void wait_until_listening(std::uint16_t port)
{
    constexpr auto kDeadline = 5s;
    constexpr auto kInterval = 5ms;
    const auto     deadline  = std::chrono::steady_clock::now() + kDeadline;

    while (std::chrono::steady_clock::now() < deadline) {
        if (can_connect(port)) {
            return;
        }
        std::this_thread::sleep_for(kInterval);
    }

    FAIL(std::format("server did not start listening on port {}", port));
}

std::string http_roundtrip(std::uint16_t port, std::string_view request_str)
{
    asio::io_context      ioc;
    asio::ip::tcp::socket s{ioc};
    asio::error_code      ec;
    auto const            ep = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port};
    ec                       = s.connect(ep, ec);
    if (ec) {
        return {};
    }
    std::size_t const bytes_sent =
        asio::write(s, asio::buffer(request_str.data(), request_str.size()), ec);
    if (ec || bytes_sent != request_str.size()) {
        return {};
    }

    std::string       response;
    asio::streambuf   buf;
    std::size_t const bytes_received = asio::read(s, buf, asio::transfer_at_least(1), ec);
    response.assign(asio::buffers_begin(buf.data()),
                    asio::buffers_begin(buf.data()) + static_cast<std::ptrdiff_t>(bytes_received));
    return response;
}

std::string http_get_with_traceparent(std::uint16_t port, std::string_view path,
                                      std::string_view traceparent)
{
    const std::string request =
        std::format("GET {} HTTP/1.0\r\nHost: localhost\r\ntraceparent: {}\r\n\r\n", path,
                    traceparent);
    return http_roundtrip(port, request);
}

struct TestServer
{
    explicit TestServer(std::uint16_t p, auto configure_fn, aevox::AppConfig cfg = {})
        : app{std::move(cfg)}, port{p}
    {
        configure_fn(app);
        thread = std::jthread{[this] { app.listen(port); }};
        wait_until_listening(port);
    }

    ~TestServer()
    {
        app.stop();
    }

    TestServer(const TestServer&)            = delete;
    TestServer& operator=(const TestServer&) = delete;
    TestServer(TestServer&&)                 = delete;
    TestServer& operator=(TestServer&&)      = delete;

    aevox::App    app;
    std::uint16_t port;
    std::jthread  thread;
};

std::filesystem::path make_temp_log_path()
{
    static std::size_t counter = 0;
    return std::filesystem::temp_directory_path() /
           std::format("aevox-test-trace-propagation-{}-{}.log", counter++,
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

TEST_CASE("trace_context - returns forwarding string_view", "[integration][log][tracing]")
{
    const auto       port     = free_port();
    const auto       log_path = make_temp_log_path();
    aevox::AppConfig cfg;
    cfg.logging.sinks = {
        aevox::FileSinkConfig{.path = log_path.string(), .format = aevox::LogFormat::JSON}};

    const std::string expected_traceparent =
        "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";

    // The handler will echo the trace_context value back in the response body.
    std::string captured_traceparent;

    {
        const TestServer server{
            port,
            [&](aevox::App& app) {
                app.get("/echo-trace", [&](aevox::Request& req) -> aevox::Task<aevox::Response> {
                    auto ctx = req.trace_context();
                    if (ctx) {
                        captured_traceparent = std::string{*ctx};
                    }
                    co_return aevox::Response::ok(ctx ? std::string{*ctx} : "none");
                });
            },
            std::move(cfg)};

        const auto resp = http_get_with_traceparent(port, "/echo-trace", expected_traceparent);
        REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
        REQUIRE(resp.find(expected_traceparent) != std::string::npos);
    }

    REQUIRE(captured_traceparent == expected_traceparent);
}

TEST_CASE("trace_context - returns none when no traceparent", "[integration][log][tracing]")
{
    const auto       port     = free_port();
    const auto       log_path = make_temp_log_path();
    aevox::AppConfig cfg;
    cfg.logging.sinks = {
        aevox::FileSinkConfig{.path = log_path.string(), .format = aevox::LogFormat::JSON}};

    {
        const TestServer server{
            port,
            [&](aevox::App& app) {
                app.get("/echo-trace", [&](aevox::Request& req) -> aevox::Task<aevox::Response> {
                    auto ctx = req.trace_context();
                    co_return aevox::Response::ok(ctx ? std::string{*ctx} : "none");
                });
            },
            std::move(cfg)};

        const std::string request =
            std::format("GET /echo-trace HTTP/1.0\r\nHost: localhost\r\n\r\n");
        const auto resp = http_roundtrip(port, request);
        REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
        REQUIRE(resp.find("none") != std::string::npos);
    }
}
