// Integration tests: WebSocket upgrade handshake over real loopback TCP.
// Uses real aevox::App + Aevox-owned test support client. No mocks.
// ADD ref: Tasks/architecture/AEV-010-arch.md §8 (Integration Tests)
#include <aevox/app.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/websocket_handler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <latch>
#include <string>
#include <thread>

#include "support/websocket_test_client.hpp"

using namespace std::chrono_literals;

namespace {

/// Builds a valid HTTP/1.1 WebSocket upgrade request string.
std::string ws_upgrade_request(std::uint16_t port, std::string_view path,
                               std::string_view key = "dGhlIHNhbXBsZSBub25jZQ==")
{
    return std::format("GET {} HTTP/1.1\r\n"
                       "Host: localhost:{}\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "Sec-WebSocket-Key: {}\r\n"
                       "Sec-WebSocket-Version: 13\r\n"
                       "\r\n",
                       path, port, key);
}

/// Builds an HTTP/1.1 upgrade request missing the Sec-WebSocket-Key header.
std::string ws_upgrade_request_no_key(std::uint16_t port, std::string_view path)
{
    return std::format("GET {} HTTP/1.1\r\n"
                       "Host: localhost:{}\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "Sec-WebSocket-Version: 13\r\n"
                       "\r\n",
                       path, port);
}

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

} // anonymous namespace

// =============================================================================
// Integration tests
// =============================================================================

TEST_CASE("WebSocket upgrade - full handshake over loopback", "[websocket][integration]")
{
    const auto port = aevox::test::free_loopback_port();

    const TestServer server{
        port, [](aevox::App& app) {
            app.ws("/ws", aevox::WebSocketHandler{
                              .on_open    = [](aevox::WebSocket&) {},
                              .on_message = [](aevox::WebSocket& ws,
                                               std::string_view  msg) { ws.send_nowait(msg); },
                              .on_close   = [](aevox::WebSocket&, std::uint16_t) {},
                          });
        }};

    // Build and send upgrade request.
    const std::string req  = ws_upgrade_request(port, "/ws");
    const auto        resp = aevox::test::WebSocketTestClient::request(port, req, 2s);

    REQUIRE(resp);
    REQUIRE(resp->find("HTTP/1.1 101") != std::string::npos);
    REQUIRE(resp->find("Switching Protocols") != std::string::npos);

    REQUIRE(resp->find("Sec-WebSocket-Accept:") != std::string::npos);

    REQUIRE(resp->find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
}

TEST_CASE("WebSocket upgrade - invalid handshake returns HTTP 400", "[websocket][integration]")
{
    const auto port = aevox::test::free_loopback_port();

    const TestServer server{port,
                            [](aevox::App& app) { app.ws("/ws", aevox::WebSocketHandler{}); }};

    // Send upgrade request without Sec-WebSocket-Key.
    const std::string req  = ws_upgrade_request_no_key(port, "/ws");
    const auto        resp = aevox::test::WebSocketTestClient::request(port, req, 2s);

    REQUIRE(resp);
    REQUIRE(resp->find("HTTP/1.1 400") != std::string::npos);
}
