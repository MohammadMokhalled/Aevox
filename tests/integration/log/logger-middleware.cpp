// logger-middleware.cpp: verify AEV-029 logger middleware over real loopback HTTP
// ADD ref: Tasks/architecture/AEV-029-arch.md §8

#include <aevox/app.hpp>
#include <aevox/middleware/logger.hpp>
#include <aevox/response.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {

[[nodiscard]] std::filesystem::path make_temp_log_path()
{
    static int counter{0};
    return std::filesystem::temp_directory_path() /
           std::format("aevox-aev029-middleware-{}-{}.log", counter++,
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

[[nodiscard]] std::uint16_t free_port()
{
    asio::io_context              ioc;
    asio::ip::tcp::acceptor const acceptor{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return acceptor.local_endpoint().port();
}

[[nodiscard]] bool can_connect(std::uint16_t port)
{
    asio::io_context      ioc;
    asio::ip::tcp::socket socket{ioc};
    asio::error_code      ec;
    const auto endpoint = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port};
    ec                  = socket.connect(endpoint, ec);
    return !ec;
}

void wait_until_listening(std::uint16_t port)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (can_connect(port)) {
            return;
        }
        std::this_thread::sleep_for(5ms);
    }
    FAIL(std::format("server did not start listening on port {}", port));
}

[[nodiscard]] std::string http_get(std::uint16_t port, std::string_view path,
                                   std::string_view extra_header = {})
{
    asio::io_context      ioc;
    asio::ip::tcp::socket socket{ioc};
    asio::error_code      ec;
    const auto endpoint = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port};
    ec                  = socket.connect(endpoint, ec);
    if (ec) {
        return {};
    }

    const std::string request =
        std::format("GET {} HTTP/1.0\r\nHost: localhost\r\n{}\r\n", path, extra_header);
    const auto sent = asio::write(socket, asio::buffer(request), ec);
    if (ec || sent != request.size()) {
        return {};
    }

    asio::streambuf buffer;
    const auto      received = asio::read(socket, buffer, asio::transfer_at_least(1), ec);
    std::string     response;
    response.assign(asio::buffers_begin(buffer.data()),
                    asio::buffers_begin(buffer.data()) + static_cast<std::ptrdiff_t>(received));
    return response;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path)
{
    std::ifstream file{path};
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] aevox::AppConfig file_log_config(const std::filesystem::path& path)
{
    aevox::AppConfig config;
    config.logging.destination   = aevox::LogDestination::File;
    config.logging.file_path     = path.string();
    config.logging.format        = aevox::LogFormat::Json;
    config.logging.level         = aevox::LogLevel::Trace;
    config.executor.thread_count = 1;
    return config;
}

} // namespace

TEST_CASE("AEV-029: logger middleware emits one access line for a successful request",
          "[integration][log]")
{
    const auto path = make_temp_log_path();
    aevox::App app{file_log_config(path)};
    app.use(aevox::middleware::logger());
    app.get("/hello", [](aevox::Request&) { return aevox::Response::ok("hello"); });

    const auto   port = free_port();
    std::jthread server{[&app, port] { app.listen(port); }};
    wait_until_listening(port);

    const auto response = http_get(port, "/hello");
    app.stop();
    server.join();
    REQUIRE(response.find("HTTP/1.1 200") != std::string::npos);

    const auto content = read_file(path);
    std::filesystem::remove(path);
    REQUIRE(content.find("\"method\":\"GET\"") != std::string::npos);
    REQUIRE(content.find("\"path\":\"/hello\"") != std::string::npos);
    REQUIRE(content.find("\"status\":200") != std::string::npos);
    REQUIRE(content.find("\"request_id\":\"000000000000") != std::string::npos);
}

TEST_CASE("AEV-029: logger middleware excludes configured paths", "[integration][log]")
{
    const auto path = make_temp_log_path();
    aevox::App app{file_log_config(path)};
    app.use(aevox::middleware::logger({.exclude_paths = {"/health"}}));
    app.get("/health", [](aevox::Request&) { return aevox::Response::ok("ok"); });
    app.get("/api", [](aevox::Request&) { return aevox::Response::ok("ok"); });

    const auto   port = free_port();
    std::jthread server{[&app, port] { app.listen(port); }};
    wait_until_listening(port);

    (void)http_get(port, "/health");
    (void)http_get(port, "/api");
    app.stop();
    server.join();

    const auto content = read_file(path);
    std::filesystem::remove(path);
    REQUIRE(content.find("\"path\":\"/health\"") == std::string::npos);
    REQUIRE(content.find("\"path\":\"/api\"") != std::string::npos);
}

TEST_CASE("AEV-029: slow request is logged at warn level", "[integration][log]")
{
    const auto path = make_temp_log_path();
    aevox::App app{file_log_config(path)};
    app.use(aevox::middleware::logger(
        {.slow_request_threshold = std::chrono::milliseconds{1}, .log_slow_requests = true}));
    app.get("/slow", [](aevox::Request&) {
        std::this_thread::sleep_for(5ms);
        return aevox::Response::ok("slow");
    });

    const auto   port = free_port();
    std::jthread server{[&app, port] { app.listen(port); }};
    wait_until_listening(port);

    (void)http_get(port, "/slow");
    app.stop();
    server.join();

    const auto content = read_file(path);
    std::filesystem::remove(path);
    REQUIRE(content.find("\"level\":\"WARN\"") != std::string::npos);
    REQUIRE(content.find("\"path\":\"/slow\"") != std::string::npos);
}

TEST_CASE("AEV-029: traceparent fields appear in request access log", "[integration][log]")
{
    const auto path = make_temp_log_path();
    aevox::App app{file_log_config(path)};
    app.use(aevox::middleware::logger());
    app.get("/trace", [](aevox::Request&) { return aevox::Response::ok("trace"); });

    const auto   port = free_port();
    std::jthread server{[&app, port] { app.listen(port); }};
    wait_until_listening(port);

    constexpr std::string_view kTraceparent{
        "traceparent: 00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01\r\n"};
    (void)http_get(port, "/trace", kTraceparent);
    app.stop();
    server.join();

    const auto content = read_file(path);
    std::filesystem::remove(path);
    REQUIRE(content.find("\"trace_id\":\"4bf92f3577b34da6a3ce929d0e0e4736\"") != std::string::npos);
    REQUIRE(content.find("\"span_id\":\"00f067aa0ba902b7\"") != std::string::npos);
}

TEST_CASE("AEV-029: file logger startup failure does not abort app startup", "[integration][log]")
{
    aevox::AppConfig config;
    config.logging.destination   = aevox::LogDestination::File;
    config.logging.file_path     = std::nullopt;
    config.executor.thread_count = 1;

    aevox::App app{config};
    app.get("/alive", [](aevox::Request&) { return aevox::Response::ok("alive"); });

    const auto   port = free_port();
    std::jthread server{[&app, port] { app.listen(port); }};
    wait_until_listening(port);

    const auto response = http_get(port, "/alive");
    app.stop();
    server.join();

    REQUIRE(response.find("HTTP/1.1 200") != std::string::npos);
}
