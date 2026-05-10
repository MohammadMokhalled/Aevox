// Integration tests: WebSocket echo and close handshake over real loopback TCP.
// Uses real aevox::App and Aevox-owned test support client. No mocks.
// ADD ref: Tasks/architecture/AEV-010-arch.md §8 (Integration Tests)
#include <aevox/app.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/websocket_handler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <latch>
#include <string>
#include <thread>

#include "support/websocket_test_client.hpp"

using namespace std::chrono_literals;

namespace {

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
    const auto port = aevox::test::free_loopback_port();

    const TestServer server{
        port, [](aevox::App& app) {
            app.ws("/echo", aevox::WebSocketHandler{
                                .on_message = [](aevox::WebSocket& ws,
                                                 std::string_view  msg) { ws.send_nowait(msg); },
                            });
        }};

    auto client = aevox::test::WebSocketTestClient::connect(port, "/echo", 2s);
    REQUIRE(client);

    const std::array<std::string, 3> messages{"frame1", "frame2", "frame3"};
    for (const auto& msg : messages) {
        auto send_result = client->send_text(msg, 2s);
        REQUIRE(send_result);
    }

    for (const auto& expected : messages) {
        auto echo = client->read_text(2s);
        REQUIRE(echo);
        REQUIRE(*echo == expected);
    }

    client->close();
}

TEST_CASE("WebSocket echo - split client frame waits for complete payload",
          "[websocket][integration]")
{
    const auto port = aevox::test::free_loopback_port();

    const TestServer server{
        port, [](aevox::App& app) {
            app.ws("/echo", aevox::WebSocketHandler{
                                .on_message = [](aevox::WebSocket& ws,
                                                 std::string_view  msg) { ws.send_nowait(msg); },
                            });
        }};

    auto client = aevox::test::WebSocketTestClient::connect(port, "/echo", 2s);
    REQUIRE(client);

    auto first_part = client->send_text_first_part("split-frame", 6U, 2s);
    REQUIRE(first_part);

    auto early_data = client->has_readable_data(50ms);
    REQUIRE(early_data);
    REQUIRE_FALSE(*early_data);

    auto second_part = client->send_text_second_part("split-frame", 6U, 2s);
    REQUIRE(second_part);

    auto echo = client->read_text(2s);
    REQUIRE(echo);
    REQUIRE(*echo == "split-frame");

    client->close();
}

TEST_CASE("WebSocket echo - close handshake completes cleanly", "[websocket][integration]")
{
    const auto port = aevox::test::free_loopback_port();

    const TestServer server{port, [](aevox::App& app) {
                                app.ws("/echo", aevox::WebSocketHandler{
                                                    .on_close = [](aevox::WebSocket&,
                                                                   std::uint16_t /*code*/) {},
                                                });
                            }};

    auto client = aevox::test::WebSocketTestClient::connect(port, "/echo", 2s);
    REQUIRE(client);

    auto close_send = client->send_close(1000, 2s);
    REQUIRE(close_send);

    auto close_code = client->read_close(2s);
    REQUIRE(close_code);
    REQUIRE(*close_code == 1000);

    client->close();
}
