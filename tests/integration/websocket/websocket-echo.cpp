// Integration tests: WebSocket echo and close handshake over real loopback TCP.
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

/// Builds and sends a valid HTTP/1.1 WebSocket upgrade request on `sock`.
/// Returns the HTTP response string.
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

    // Read until we see the end of HTTP response headers (\r\n\r\n).
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
/// Masking key: {0x00, 0x00, 0x00, 0x00} for simplicity (XOR with 0 = identity).
std::vector<std::byte> make_masked_text_frame(std::string_view payload)
{
    const std::size_t      payload_len = payload.size();
    std::vector<std::byte> frame;

    frame.push_back(std::byte{0x81}); // FIN=1, opcode=text

    // Length byte with MASK=1.
    if (payload_len < 126) {
        frame.push_back(static_cast<std::byte>(0x80U | payload_len));
    }
    else {
        // 16-bit extended length.
        frame.push_back(std::byte{0xFE});
        frame.push_back(static_cast<std::byte>(payload_len >> 8U));
        frame.push_back(static_cast<std::byte>(payload_len & 0xFFU));
    }

    // Masking key = {0x00, 0x00, 0x00, 0x00} — XOR with zero is identity.
    frame.push_back(std::byte{0x00});
    frame.push_back(std::byte{0x00});
    frame.push_back(std::byte{0x00});
    frame.push_back(std::byte{0x00});

    // Payload (masked with 0x00 = unchanged).
    for (char c : payload) {
        frame.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }

    return frame;
}

/// Builds a client Close frame with code 1000.
std::vector<std::byte> make_close_frame()
{
    // Close frame: FIN=1, opcode=0x8, MASK=1, length=2, mask={0,0,0,0}, payload={0x03,0xE8}
    // 0x03E8 big-endian = 1000
    std::vector<std::byte> frame{
        std::byte{0x88},                                                    // FIN=1, opcode=Close
        std::byte{0x82},                                                    // MASK=1, length=2
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, // mask
        std::byte{0x03}, std::byte{0xE8},                                   // code=1000
    };
    return frame;
}

/// Reads exactly one WebSocket frame from the socket (server frame, unmasked).
/// Returns the payload as a string.
std::string read_server_frame(asio::ip::tcp::socket& sock)
{
    asio::error_code ec;

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
        for (std::size_t i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8U) | ext_spn[i];
        }
    }

    // Read payload.
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

TEST_CASE("WebSocket echo - send and receive text frame over loopback", "[websocket][integration]")
{
    const auto port = free_port();

    const TestServer server{
        port, [](aevox::App& app) {
            app.ws("/echo", aevox::WebSocketHandler{
                                .on_message = [](aevox::WebSocket& ws,
                                                 std::string_view  msg) { ws.send_nowait(msg); },
                            });
        }};

    asio::io_context      ioc;
    asio::ip::tcp::socket client{ioc};
    asio::error_code      ec;
    client.connect(asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port}, ec);
    REQUIRE_FALSE(ec);

    // Perform upgrade.
    const std::string upgrade_resp = do_upgrade(client, port, "/echo");
    REQUIRE(upgrade_resp.find("101") != std::string::npos);

    // Send three masked text frames.
    const std::array<std::string, 3> messages{"frame1", "frame2", "frame3"};
    for (const auto& msg : messages) {
        auto frame = make_masked_text_frame(msg);
        asio::write(client, asio::buffer(frame.data(), frame.size()), ec);
        REQUIRE_FALSE(ec);
    }

    // Receive three echo responses and verify order.
    for (const auto& expected : messages) {
        const std::string echo = read_server_frame(client);
        REQUIRE(echo == expected);
    }
}

TEST_CASE("WebSocket echo - split client frame waits for complete payload",
          "[websocket][integration]")
{
    const auto port = free_port();

    const TestServer server{
        port, [](aevox::App& app) {
            app.ws("/echo", aevox::WebSocketHandler{
                                .on_message = [](aevox::WebSocket& ws,
                                                 std::string_view  msg) { ws.send_nowait(msg); },
                            });
        }};

    asio::io_context      ioc;
    asio::ip::tcp::socket client{ioc};
    asio::error_code      ec;
    client.connect(asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port}, ec);
    REQUIRE_FALSE(ec);

    const std::string upgrade_resp = do_upgrade(client, port, "/echo");
    REQUIRE(upgrade_resp.find("101") != std::string::npos);

    auto frame = make_masked_text_frame("split-frame");
    asio::write(client, asio::buffer(frame.data(), 6), ec);
    REQUIRE_FALSE(ec);

    std::this_thread::sleep_for(50ms);
    client.non_blocking(true, ec);
    REQUIRE_FALSE(ec);
    std::array<char, 16> early_buf{};
    asio::error_code     early_ec;
    const auto           early_n = client.read_some(asio::buffer(early_buf), early_ec);
    REQUIRE(early_n == 0);
    REQUIRE(early_ec);
    REQUIRE((early_ec == asio::error::would_block || early_ec == asio::error::try_again));

    client.non_blocking(false, ec);
    REQUIRE_FALSE(ec);
    asio::write(client, asio::buffer(frame.data() + 6, frame.size() - 6), ec);
    REQUIRE_FALSE(ec);

    const std::string echo = read_server_frame(client);
    REQUIRE(echo == "split-frame");
}

TEST_CASE("WebSocket echo - close handshake completes cleanly", "[websocket][integration]")
{
    const auto port = free_port();

    const TestServer server{port, [](aevox::App& app) {
                                app.ws("/echo", aevox::WebSocketHandler{
                                                    .on_close = [](aevox::WebSocket&,
                                                                   std::uint16_t /*code*/) {},
                                                });
                            }};

    asio::io_context      ioc;
    asio::ip::tcp::socket client{ioc};
    asio::error_code      ec;
    client.connect(asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port}, ec);
    REQUIRE_FALSE(ec);

    // Upgrade.
    const std::string upgrade_resp = do_upgrade(client, port, "/echo");
    REQUIRE(upgrade_resp.find("101") != std::string::npos);

    // Send a masked Close frame with code 1000.
    auto close_frame = make_close_frame();
    asio::write(client, asio::buffer(close_frame.data(), close_frame.size()), ec);
    REQUIRE_FALSE(ec);

    // The server should echo a Close frame.
    // Read 2 header bytes + 2 payload bytes (close code).
    std::array<std::uint8_t, 4> resp{};
    asio::read(client, asio::buffer(resp.data(), resp.size()), ec);

    // First byte should have opcode=0x8 (Close) with FIN=1 → 0x88.
    REQUIRE(resp[0] == 0x88U);
    // Length byte: no mask (server) + 2 bytes (close code) → 0x02.
    REQUIRE(resp[1] == 0x02U);
    // Close code = 1000 = 0x03E8.
    const std::uint16_t recv_code = (static_cast<std::uint16_t>(resp[2]) << 8U) | resp[3];
    REQUIRE(recv_code == 1000);
}
