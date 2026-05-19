// tests/integration/config/config-file-roundtrip.cpp
// Integration test: TOML config file is written to the real filesystem,
// App::create() reads it, a live listen/stop cycle completes without error.
// ADD ref: Tasks/architecture/AEV-025-arch.md § Test Architecture

#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/response.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {

std::string write_temp_toml(const std::string& content)
{
    auto path = std::filesystem::temp_directory_path() /
                std::format("aevox_test_{}.toml", std::random_device{}());
    std::ofstream f{path};
    f << content;
    return path.string();
}

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

std::string http_get(std::uint16_t port)
{
    asio::io_context      ioc;
    asio::ip::tcp::socket socket{ioc};
    asio::error_code      ec;
    const auto            ep = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port};
    ec                       = socket.connect(ep, ec);
    if (ec) {
        return {};
    }

    const std::string request = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    const auto        sent    = asio::write(socket, asio::buffer(request), ec);
    if (ec || sent != request.size()) {
        return {};
    }

    asio::streambuf buf;
    const auto      received = asio::read(socket, buf, asio::transfer_at_least(1), ec);
    std::string     response;
    response.assign(asio::buffers_begin(buf.data()),
                    asio::buffers_begin(buf.data()) + static_cast<std::ptrdiff_t>(received));
    return response;
}

} // namespace

TEST_CASE("Config integration - file roundtrip with real App listen", "[config][integration]")
{
    // Write a real TOML config: port=0 (OS assigns ephemeral port),
    // short timeouts so the test exits quickly.
    const std::string toml_content = R"(
max_read_bytes = 8192
max_header_count = 50

[executor]
thread_count     = 2
cpu_pool_threads = 0
drain_timeout    = 2
)";
    const auto        path         = write_temp_toml(toml_content);

    // base_config provides the port (0 = ephemeral) and a short request timeout.
    aevox::AppConfig base;
    base.port                   = 0; // ephemeral port
    base.request_timeout        = 2s;
    base.executor.drain_timeout = 2s;

    auto result = aevox::App::create(base, std::string_view{path});
    std::remove(path.c_str());

    REQUIRE(result.has_value());

    auto&       app = *result;
    const auto& cfg = app.config();

    // Verify file values were applied.
    CHECK(cfg.max_read_bytes == 8192);
    CHECK(cfg.max_header_count == 50);
    CHECK(cfg.executor.thread_count == 2);
    CHECK(cfg.executor.cpu_pool_threads == 0);
    CHECK(cfg.executor.drain_timeout == 2s);
    // Base config values are preserved.
    CHECK(cfg.port == 0);

    // Register a trivial handler and perform a listen/stop lifecycle.
    app.get("/", [](aevox::Request&) { return aevox::Response::ok("ok"); });

    const auto   port            = free_port();
    bool         listen_returned = false;
    std::jthread server{[&app, &listen_returned, port] {
        app.listen(port);
        listen_returned = true;
    }};

    wait_until_listening(port);
    const auto response = http_get(port);
    CHECK(response.find("HTTP/1.1 200") != std::string::npos);
    CHECK(response.find("ok") != std::string::npos);

    app.stop();

    server.join();
    CHECK(listen_returned);
}
