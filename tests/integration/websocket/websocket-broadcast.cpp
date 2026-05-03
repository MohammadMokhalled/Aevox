// Integration tests: WebSocket pub/sub broadcast over real loopback TCP.
// Uses real aevox::App and raw Asio TCP sockets as client. No mocks.
// ADD ref: Tasks/architecture/AEV-010-arch.md §8 (Integration Tests)
#include <aevox/app.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/websocket_handler.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <latch>
#include <span>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// =============================================================================
// WebSocket raw-frame client helpers
// =============================================================================

namespace {

std::uint16_t free_port()
{
    asio::io_context              ioc;
    asio::ip::tcp::acceptor const a{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return a.local_endpoint().port();
}

/// Performs WebSocket upgrade on the given socket, returns the HTTP response.
std::string do_upgrade(asio::ip::tcp::socket& sock, std::uint16_t port, std::string_view path,
                       std::string_view key = "dGhlIHNhbXBsZSBub25jZQ==")
{
    const std::string req = std::format("GET {} HTTP/1.1\r\n"
                                        "Host: localhost:{}\r\n"
                                        "Upgrade: websocket\r\n"
                                        "Connection: Upgrade\r\n"
                                        "Sec-WebSocket-Key: {}\r\n"
                                        "Sec-WebSocket-Version: 13\r\n"
                                        "\r\n",
                                        path, port, key);

    asio::error_code ec;
    asio::write(sock, asio::buffer(req), ec);
    if (ec)
        return {};

    std::string         resp;
    std::array<char, 1> byte_buf{};
    while (resp.find("\r\n\r\n") == std::string::npos) {
        const auto n = asio::read(sock, asio::buffer(byte_buf), ec);
        if (ec || n == 0)
            break;
        resp += byte_buf[0];
    }
    return resp;
}

/// Builds a client-to-server masked text frame.
/// Masking key: {0x00, 0x00, 0x00, 0x00} for simplicity.
std::vector<std::byte> make_masked_text_frame(std::string_view payload)
{
    const std::size_t      payload_len = payload.size();
    std::vector<std::byte> frame;

    frame.push_back(std::byte{0x81}); // FIN=1, opcode=text

    if (payload_len < 126) {
        frame.push_back(static_cast<std::byte>(0x80U | payload_len));
    }
    else {
        frame.push_back(std::byte{0xFE});
        frame.push_back(static_cast<std::byte>(payload_len >> 8U));
        frame.push_back(static_cast<std::byte>(payload_len & 0xFFU));
    }

    // Masking key = {0x00, 0x00, 0x00, 0x00}
    frame.push_back(std::byte{0x00});
    frame.push_back(std::byte{0x00});
    frame.push_back(std::byte{0x00});
    frame.push_back(std::byte{0x00});

    for (char c : payload) {
        frame.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }

    return frame;
}

/// Reads one WebSocket text frame from the socket with a timeout.
/// Returns the payload as a string, or empty on error/timeout.
std::string read_server_frame_with_timeout(
    asio::ip::tcp::socket& sock, [[maybe_unused]] std::chrono::milliseconds timeout = 2000ms)
{
    asio::error_code ec;

    // Set a read deadline using a timer in the io_context.
    // Simple blocking read with SO_RCVTIMEO is simpler for tests.
    sock.set_option(asio::socket_base::receive_buffer_size{65536}, ec);

    // Read first 2 header bytes.
    std::array<std::uint8_t, 2> header{};
    asio::read(sock, asio::buffer(header.data(), 2), ec);
    if (ec)
        return {};

    const std::uint8_t byte1       = header[1];
    std::uint64_t      payload_len = byte1 & 0x7FU;

    if (payload_len == 126) {
        std::array<std::uint8_t, 2> ext{};
        asio::read(sock, asio::buffer(ext.data(), 2), ec);
        if (ec)
            return {};
        payload_len = (static_cast<std::uint64_t>(ext[0]) << 8U) | ext[1];
    }
    else if (payload_len == 127) {
        std::array<std::uint8_t, 8> ext{};
        asio::read(sock, asio::buffer(ext.data(), 8), ec);
        if (ec)
            return {};
        payload_len        = 0;
        const auto ext_spn = std::span<const std::uint8_t>{ext};
        for (std::size_t i = 0; i < 8; ++i)
            payload_len = (payload_len << 8U) | ext_spn[i];
    }

    std::string payload(payload_len, '\0');
    if (payload_len > 0) {
        asio::read(sock, asio::buffer(payload.data(), payload_len), ec);
        if (ec)
            return {};
    }
    return payload;
}

// =============================================================================
// TestServer
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

TEST_CASE("WebSocket broadcast - two clients on same topic receive message",
          "[websocket][integration]")
{
    const auto port = free_port();

    const TestServer server{
        port, [](aevox::App& app) {
            app.ws("/room/{id}",
                   aevox::WebSocketHandler{
                       .on_open    = [](aevox::WebSocket& ws) { ws.subscribe(ws.topic()); },
                       .on_message = [](aevox::WebSocket& ws,
                                        std::string_view  msg) { ws.publish(ws.topic(), msg); },
                   });
        }};

    // Connect two raw TCP clients to the same room.
    asio::io_context ioc;

    asio::ip::tcp::socket client_a{ioc};
    asio::ip::tcp::socket client_b{ioc};
    asio::error_code      ec;

    const auto ep = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port};

    client_a.connect(ep, ec);
    REQUIRE_FALSE(ec);

    client_b.connect(ep, ec);
    REQUIRE_FALSE(ec);

    // Both clients upgrade to the same room.
    const std::string resp_a =
        do_upgrade(client_a, port, "/room/test-room", "dGhlIHNhbXBsZSBub25jZQ==");
    REQUIRE(resp_a.find("101") != std::string::npos);

    const std::string resp_b =
        do_upgrade(client_b, port, "/room/test-room", "c2VjcmV0a2V5MTIzNDU2Nzg5MA==");
    REQUIRE(resp_b.find("101") != std::string::npos);

    // Give the server time to process both upgrades and set up subscriptions.
    std::this_thread::sleep_for(50ms);

    // Client A sends "broadcast" — should be delivered to Client B only.
    auto msg_frame = make_masked_text_frame("broadcast");
    asio::write(client_a, asio::buffer(msg_frame.data(), msg_frame.size()), ec);
    REQUIRE_FALSE(ec);

    // Client B should receive "broadcast".
    const std::string received = read_server_frame_with_timeout(client_b);
    REQUIRE(received == "broadcast");
}
