// App + Router integration tests — full stack over real loopback TCP.
// ADD ref: Tasks/architecture/AEV-004-arch.md §8.2
//
// Uses real aevox::App on an ephemeral port. Client side uses raw Asio sockets. No mocks.

#include <aevox/app.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <format>
#include <string>
#include <thread>

using namespace std::chrono_literals;

// =============================================================================
// Helpers
// =============================================================================

namespace {

/// Returns an available ephemeral port.
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
}

/// Sends `request_str` to localhost:port and returns the full response as string.
std::string http_roundtrip(std::uint16_t port, std::string_view request_str)
{
    asio::io_context      ioc;
    asio::ip::tcp::socket s{ioc};
    asio::error_code      ec;
    auto const            ep = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port};
    ec                       = s.connect(ep, ec);
    if (ec)
        return {};
    std::size_t const bytes_sent =
        asio::write(s, asio::buffer(request_str.data(), request_str.size()), ec);
    if (ec || bytes_sent != request_str.size())
        return {};

    std::string     response;
    asio::streambuf buf;
    // Read until the server closes the connection (Connection: close) or EOF.
    std::size_t const bytes_received = asio::read(s, buf, asio::transfer_at_least(1), ec);
    response.assign(asio::buffers_begin(buf.data()),
                    asio::buffers_begin(buf.data()) + static_cast<std::ptrdiff_t>(bytes_received));
    return response;
}

/// Minimal HTTP/1.0 GET request (Connection: close so server closes after reply).
std::string http_get(std::uint16_t port, std::string_view path)
{
    return http_roundtrip(port, std::format("GET {} HTTP/1.0\r\nHost: localhost\r\n\r\n", path));
}

std::string http_post(std::uint16_t port, std::string_view path, std::string_view body = {})
{
    return http_roundtrip(
        port, std::format("POST {} HTTP/1.0\r\nHost: localhost\r\nContent-Length: {}\r\n\r\n{}",
                          path, body.size(), body));
}

} // namespace

// =============================================================================
// TestServer — starts App in a background thread, stops on destruction.
//
// configure_fn is called against app BEFORE listen() starts, satisfying the
// Router thread-safety contract: all registration must complete before any
// dispatch() call can occur. (MAJOR-2 fix from architect review)
// =============================================================================

struct TestServer
{
    explicit TestServer(std::uint16_t p, auto configure_fn) : port{p}
    {
        configure_fn(app); // register routes before listen()
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

    aevox::App    app{aevox::AppConfig{.executor = {.thread_count = 2, .drain_timeout = 2s}}};
    std::uint16_t port;
    std::jthread  thread;
};

// =============================================================================
// Integration tests
// =============================================================================

TEST_CASE("static route returns 200", "[integration][router]")
{
    const auto       port = free_port();
    TestServer const server{port, [](aevox::App& app) {
                                app.get("/hello", [](aevox::Request&) {
                                    return aevox::Response::ok("Hello, World!");
                                });
                            }};

    const auto resp = http_get(port, "/hello");
    REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
    REQUIRE(resp.find("Hello, World!") != std::string::npos);
}

TEST_CASE("unregistered path returns 404", "[integration][router]")
{
    const auto       port = free_port();
    TestServer const server{port, [](aevox::App& app) {
                                app.get("/hello",
                                        [](aevox::Request&) { return aevox::Response::ok("x"); });
                            }};

    const auto resp = http_get(port, "/notfound");
    REQUIRE(resp.find("HTTP/1.1 404") != std::string::npos);
}

TEST_CASE("wrong method returns 405 with Allow header", "[integration][router]")
{
    const auto       port = free_port();
    TestServer const server{port, [](aevox::App& app) {
                                app.get("/resource",
                                        [](aevox::Request&) { return aevox::Response::ok("x"); });
                            }};

    const auto resp = http_post(port, "/resource");
    REQUIRE(resp.find("HTTP/1.1 405") != std::string::npos);
    REQUIRE(resp.find("Allow:") != std::string::npos);
}

TEST_CASE("path parameter extracted end-to-end", "[integration][router]")
{
    const auto       port = free_port();
    TestServer const server{port, [](aevox::App& app) {
                                app.get("/users/{id:int}", [](aevox::Request&, int id) {
                                    return aevox::Response::ok(std::to_string(id));
                                });
                            }};

    const auto resp = http_get(port, "/users/99");
    REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
    REQUIRE(resp.find("99") != std::string::npos);
}

TEST_CASE("bad typed param returns 400", "[integration][router]")
{
    const auto       port = free_port();
    TestServer const server{port, [](aevox::App& app) {
                                app.get("/items/{id:int}", [](aevox::Request&, int id) {
                                    return aevox::Response::ok(std::to_string(id));
                                });
                            }};

    const auto resp = http_get(port, "/items/notanumber");
    REQUIRE(resp.find("HTTP/1.1 400") != std::string::npos);
}

TEST_CASE("wildcard captures tail path", "[integration][router]")
{
    const auto       port = free_port();
    TestServer const server{port, [](aevox::App& app) {
                                app.get("/files/{path...}",
                                        [](aevox::Request&, const std::string& path) {
                                            return aevox::Response::ok(path);
                                        });
                            }};

    const auto resp = http_get(port, "/files/docs/readme.txt");
    REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
    REQUIRE(resp.find("docs/readme.txt") != std::string::npos);
}
