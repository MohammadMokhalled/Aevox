// slow-request-warn.cpp: slow requests emit both INFO and WARN
// ADD ref: Tasks/architecture/AEV-011-arch.md § Test Architecture

#include <aevox/app.hpp>
#include <aevox/async.hpp>
#include <aevox/middleware/logger.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <latch>
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

    aevox::App    app;
    std::uint16_t port;
    std::latch    ready{1};
    std::jthread  thread;
};

std::filesystem::path make_temp_log_path()
{
    static std::size_t counter = 0;
    return std::filesystem::temp_directory_path() /
           std::format("aevox-test-slow-request-{}-{}.log", counter++,
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

TEST_CASE("slow request warn - exceeds threshold emits WARN in addition to INFO",
          "[integration][log]")
{
    const auto       port     = free_port();
    const auto       log_path = make_temp_log_path();
    aevox::AppConfig cfg;
    cfg.logging.sinks = {
        aevox::FileSinkConfig{.path = log_path.string(), .format = aevox::LogFormat::JSON}};

    {
        const TestServer server{port,
                                [&](aevox::App& app) {
                                    app.use(aevox::middleware::logger({
                                        .slow_request_threshold = 50ms,
                                    }));
                                    app.get("/slow",
                                            [](aevox::Request&) -> aevox::Task<aevox::Response> {
                                                co_await aevox::sleep(100ms);
                                                co_return aevox::Response::ok("done");
                                            });
                                },
                                std::move(cfg)};

        const auto resp = http_get(port, "/slow");
        REQUIRE(resp.find("HTTP/1.1 200") != std::string::npos);
    }

    std::ifstream file(log_path);
    REQUIRE(file.is_open());
    const std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    file.close();
    std::filesystem::remove(log_path);

    // Should contain the auto-log INFO line and the slow-request WARN line.
    REQUIRE(content.find("/slow") != std::string::npos);
    REQUIRE(content.find("Slow request") != std::string::npos);
}
