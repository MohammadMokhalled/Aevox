// Integration tests: WebSocket pub/sub broadcast over real loopback TCP.
// Uses real aevox::App and Aevox-owned test support client. No mocks.
// ADD ref: Tasks/architecture/AEV-010-arch.md §8 (Integration Tests)
#include <aevox/app.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/websocket_handler.hpp>

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("WebSocket broadcast - two clients on same topic receive message",
          "[websocket][integration]")
{
    const auto port = aevox::test::free_loopback_port();

    const TestServer server{
        port, [](aevox::App& app) {
            app.ws("/room/{id}",
                   aevox::WebSocketHandler{
                       .on_open    = [](aevox::WebSocket& ws) { ws.subscribe(ws.topic()); },
                       .on_message = [](aevox::WebSocket& ws,
                                        std::string_view  msg) { ws.publish(ws.topic(), msg); },
                   });
        }};

    auto client_a = aevox::test::WebSocketTestClient::connect(port, "/room/test-room", 2s);
    REQUIRE(client_a);

    auto client_b = aevox::test::WebSocketTestClient::connect(port, "/room/test-room", 2s);
    REQUIRE(client_b);

    auto send_result = client_a->send_text("broadcast", 2s);
    REQUIRE(send_result);

    auto received = client_b->read_text(2s);
    REQUIRE(received);
    REQUIRE(*received == "broadcast");

    auto self_data = client_a->has_readable_data(50ms);
    REQUIRE(self_data);
    REQUIRE_FALSE(*self_data);

    client_a->close();
    client_b->close();
}
