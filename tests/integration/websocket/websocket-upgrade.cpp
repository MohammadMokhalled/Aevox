// Integration tests: WebSocket upgrade handshake over real loopback TCP.
// Uses real aevox::App + real asio::io_context raw TCP client. No mocks.
// ADD ref: Tasks/architecture/AEV-010-arch.md §8 (Integration Tests)
#include <aevox/app.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/websocket_handler.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <latch>
#include <string>
#include <thread>

using namespace std::chrono_literals;

// =============================================================================
// Helpers — raw WebSocket client over TCP
// =============================================================================

namespace {

/// Returns an available ephemeral port.
std::uint16_t free_port()
{
    asio::io_context              ioc;
    asio::ip::tcp::acceptor const a{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return a.local_endpoint().port();
}

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

/// Sends request_str to localhost:port and returns the server's full response.
std::string tcp_request(std::uint16_t port, std::string_view request_str)
{
    asio::io_context      ioc;
    asio::ip::tcp::socket s{ioc};
    asio::error_code      ec;
    auto const            ep = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port};
    s.connect(ep, ec);
    if (ec)
        return {};
    asio::write(s, asio::buffer(request_str.data(), request_str.size()), ec);
    if (ec)
        return {};

    std::string     response;
    asio::streambuf buf;
    // Read until EOF or as much as available.
    asio::read(s, buf, asio::transfer_at_least(1), ec);
    response.assign(asio::buffers_begin(buf.data()), asio::buffers_end(buf.data()));
    return response;
}

// =============================================================================
// TestServer — spins up aevox::App in a background thread.
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
        std::this_thread::sleep_for(30ms);
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
    const auto port = free_port();

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
    const std::string resp = tcp_request(port, req);

    // The response must start with "HTTP/1.1 101 Switching Protocols".
    REQUIRE_FALSE(resp.empty());
    REQUIRE(resp.find("HTTP/1.1 101") != std::string::npos);
    REQUIRE(resp.find("Switching Protocols") != std::string::npos);

    // The Sec-WebSocket-Accept header must be present.
    REQUIRE(resp.find("Sec-WebSocket-Accept:") != std::string::npos);

    // For the known key "dGhlIHNhbXBsZSBub25jZQ==", the expected accept is:
    REQUIRE(resp.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
}

TEST_CASE("WebSocket upgrade - invalid handshake returns HTTP 400", "[websocket][integration]")
{
    const auto port = free_port();

    const TestServer server{port,
                            [](aevox::App& app) { app.ws("/ws", aevox::WebSocketHandler{}); }};

    // Send upgrade request without Sec-WebSocket-Key.
    const std::string req  = ws_upgrade_request_no_key(port, "/ws");
    const std::string resp = tcp_request(port, req);

    // The response must be HTTP 400.
    REQUIRE_FALSE(resp.empty());
    REQUIRE(resp.find("HTTP/1.1 400") != std::string::npos);
}
