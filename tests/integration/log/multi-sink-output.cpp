// multi-sink-output.cpp: two file sinks receive identical entries
// ADD ref: Tasks/architecture/AEV-011-arch.md § Test Architecture

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
    if (ec)
        return {};
    std::size_t const bytes_sent =
        asio::write(s, asio::buffer(request_str.data(), request_str.size()), ec);
    if (ec || bytes_sent != request_str.size())
        return {};

    std::string       response;
    asio::streambuf   buf;
    std::size_t const bytes_received = asio::read(s, buf, asio::transfer_at_least(1), ec);
    response.assign(asio::buffers_begin(buf.data()),
                    asio::buffers_begin(buf.data()) + static_cast<std::ptrdiff_t>(bytes_received));
    return response;
}

std::string http_get(std::uint16_t port, std::string_view path)
{
    return http_roundtrip(port, std::format("GET {} HTTP/1.0\r\nHost: localhost\r\n\r\n", path));
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
           std::format("aevox-test-multi-sink-{}-{}.log", counter++,
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

TEST_CASE("multi-sink output - two file sinks receive identical entries", "[integration][log]")
{
    const auto       port      = free_port();
    const auto       log_path1 = make_temp_log_path();
    const auto       log_path2 = make_temp_log_path();
    aevox::AppConfig cfg;
    cfg.logging.sinks = {
        aevox::FileSinkConfig{.path = log_path1.string(), .format = aevox::LogFormat::JSON},
        aevox::FileSinkConfig{.path = log_path2.string(), .format = aevox::LogFormat::JSON},
    };

    {
        const TestServer server{port,
                                [&](aevox::App& app) {
                                    app.use(aevox::middleware::logger());
                                    app.get("/api", [](aevox::Request&) {
                                        return aevox::Response::ok("data");
                                    });
                                },
                                std::move(cfg)};

        const auto resp = http_get(port, "/api");
        REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
    }

    std::ifstream file1(log_path1);
    std::ifstream file2(log_path2);
    REQUIRE(file1.is_open());
    REQUIRE(file2.is_open());

    const std::string content1((std::istreambuf_iterator<char>(file1)),
                               std::istreambuf_iterator<char>());
    const std::string content2((std::istreambuf_iterator<char>(file2)),
                               std::istreambuf_iterator<char>());
    file1.close();
    file2.close();
    std::filesystem::remove(log_path1);
    std::filesystem::remove(log_path2);

    REQUIRE(!content1.empty());
    REQUIRE(content1 == content2);
    REQUIRE(content1.find("/api") != std::string::npos);
}
