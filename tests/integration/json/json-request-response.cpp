// json-request-response.cpp: JSON request/response integration tests
// ADD ref: Tasks/architecture/AEV-009-arch.md § Test Architecture
//
// Uses real aevox::App on an ephemeral loopback port.
// Client side uses raw Asio sockets. No mocks.

#include <aevox/app.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <latch>
#include <string>
#include <thread>

// Template definitions for Request::json<T>() and Response::json<T>()
// (required for instantiation in the handler lambda's TU).
#include "http/request_impl.hpp"
#include "http/response_impl.hpp"

using namespace std::chrono_literals;

// =============================================================================
// Test fixtures — external linkage required by glaze extern template variables.
// =============================================================================

struct PersonDto
{
    std::string name;
    int         age{};
    bool        active{};
};

// =============================================================================
// Helpers — same pattern as tests/integration/router/router-e2e.cpp
// =============================================================================

namespace {

std::uint16_t free_port()
{
    asio::io_context              ioc;
    asio::ip::tcp::acceptor const a{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return a.local_endpoint().port();
}

std::string http_roundtrip(std::uint16_t port, std::string_view request_str)
{
    asio::io_context      ioc;
    asio::ip::tcp::socket s{ioc};
    asio::error_code      ec;
    const auto            ep = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port};
    ec                       = s.connect(ep, ec);
    if (ec)
        return {};
    const std::size_t bytes_sent =
        asio::write(s, asio::buffer(request_str.data(), request_str.size()), ec);
    if (ec || bytes_sent != request_str.size())
        return {};

    std::string       response;
    asio::streambuf   buf;
    const std::size_t bytes_received = asio::read(s, buf, asio::transfer_at_least(1), ec);
    response.assign(asio::buffers_begin(buf.data()),
                    asio::buffers_begin(buf.data()) + static_cast<std::ptrdiff_t>(bytes_received));
    return response;
}

std::string http_post(std::uint16_t port, std::string_view path, std::string_view body)
{
    return http_roundtrip(port, std::format("POST {} HTTP/1.0\r\nHost: localhost\r\n"
                                            "Content-Type: application/json\r\n"
                                            "Content-Length: {}\r\n\r\n{}",
                                            path, body.size(), body));
}

// =============================================================================
// TestServer — starts App in a background thread, stops on destruction.
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
        std::this_thread::sleep_for(20ms);
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

} // namespace

// =============================================================================
// Integration tests
// =============================================================================

TEST_CASE("Request json - valid body parses to struct", "[json][integration]")
{
    const auto       port = free_port();
    TestServer const server{
        port, [](aevox::App& app) {
            app.post("/parse", [](aevox::Request& req) -> aevox::Task<aevox::Response> {
                auto result = co_await req.json<PersonDto>();
                if (!result) {
                    co_return aevox::Response::bad_request(std::string{result.error().message()});
                }
                co_return aevox::Response::ok(
                    std::format("name={},age={}", result->name, result->age));
            });
        }};

    const auto resp = http_post(port, "/parse", R"({"name":"alice","age":30,"active":true})");
    REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
    REQUIRE(resp.find("name=alice,age=30") != std::string::npos);
}

TEST_CASE("Request json - malformed body returns expected error", "[json][integration]")
{
    const auto       port = free_port();
    TestServer const server{port, [](aevox::App& app) {
                                app.post("/parse",
                                         [](aevox::Request& req) -> aevox::Task<aevox::Response> {
                                             auto result = co_await req.json<PersonDto>();
                                             if (!result) {
                                                 co_return aevox::Response::bad_request(
                                                     std::string{result.error().message()});
                                             }
                                             co_return aevox::Response::ok("ok");
                                         });
                            }};

    const auto resp = http_post(port, "/parse", R"({"name":"alice")");
    REQUIRE(resp.find("HTTP/1.1 400") != std::string::npos);
}

TEST_CASE("Response json - serializes struct with correct Content-Type", "[json][integration]")
{
    const auto       port = free_port();
    TestServer const server{port, [](aevox::App& app) {
                                app.get("/person", [](aevox::Request& /*req*/) {
                                    return aevox::Response::json(
                                        PersonDto{.name = "bob", .age = 25, .active = false});
                                });
                            }};

    asio::io_context      ioc;
    asio::ip::tcp::socket s{ioc};
    asio::error_code      ec;
    const auto            ep = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port};
    ec                       = s.connect(ep, ec);
    REQUIRE(!ec);

    const std::string request_str = "GET /person HTTP/1.0\r\nHost: localhost\r\n\r\n";
    asio::write(s, asio::buffer(request_str), ec);
    REQUIRE(!ec);

    std::string       response;
    asio::streambuf   buf;
    const std::size_t bytes_received = asio::read(s, buf, asio::transfer_at_least(1), ec);
    response.assign(asio::buffers_begin(buf.data()),
                    asio::buffers_begin(buf.data()) + static_cast<std::ptrdiff_t>(bytes_received));

    REQUIRE(response.find("HTTP/1.1 200") != std::string::npos);
    REQUIRE(response.find("Content-Type: application/json") != std::string::npos);
    REQUIRE(response.find("\"name\"") != std::string::npos);
    REQUIRE(response.find("\"bob\"") != std::string::npos);
}
