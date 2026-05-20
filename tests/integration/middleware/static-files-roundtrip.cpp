// Static file middleware loopback integration tests.
// ADD ref: static file middleware test architecture.

#include <aevox/app.hpp>
#include <aevox/middleware/static_files.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

namespace {

std::uint16_t free_port()
{
    asio::io_context              ioc;
    asio::ip::tcp::acceptor const acceptor{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return acceptor.local_endpoint().port();
}

bool can_connect(std::uint16_t port)
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
    constexpr auto kDeadlineDuration = std::chrono::seconds{5};
    constexpr auto kInterval         = std::chrono::milliseconds{5};
    const auto     deadline          = std::chrono::steady_clock::now() + kDeadlineDuration;

    while (std::chrono::steady_clock::now() < deadline) {
        if (can_connect(port)) {
            return;
        }
        std::this_thread::sleep_for(kInterval);
    }

    FAIL(std::format("server did not start listening on port {}", port));
}

std::string http_roundtrip(std::uint16_t port, std::string_view request)
{
    asio::io_context      ioc;
    asio::ip::tcp::socket socket{ioc};
    asio::error_code      ec;
    const auto endpoint = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port};
    ec                  = socket.connect(endpoint, ec);
    if (ec) {
        return {};
    }

    const auto bytes_sent = asio::write(socket, asio::buffer(request.data(), request.size()), ec);
    if (ec || bytes_sent != request.size()) {
        return {};
    }

    std::string            response;
    std::array<char, 4096> buffer{};
    while (true) {
        const auto bytes_read = socket.read_some(asio::buffer(buffer), ec);
        if (bytes_read > 0) {
            response.append(buffer.data(), bytes_read);
        }
        if (ec == asio::error::eof) {
            break;
        }
        if (ec) {
            return {};
        }
    }

    return response;
}

std::string http_request(std::uint16_t port, std::string_view method, std::string_view path)
{
    return http_roundtrip(port,
                          std::format("{} {} HTTP/1.0\r\nHost: localhost\r\n\r\n", method, path));
}

struct TempTree
{
    TempTree()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root             = std::filesystem::temp_directory_path() /
               std::filesystem::path{"aevox-static-files-it-" + std::to_string(stamp)};
        std::filesystem::create_directories(root);
    }

    ~TempTree()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    TempTree(const TempTree&)            = delete;
    TempTree& operator=(const TempTree&) = delete;
    TempTree(TempTree&&)                 = delete;
    TempTree& operator=(TempTree&&)      = delete;

    void write(std::string_view relative, std::string_view body) const
    {
        auto path = root / std::filesystem::path{std::string{relative}};
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file{path, std::ios::binary};
        file << body;
    }

    std::filesystem::path root;
};

struct TestServer
{
    explicit TestServer(std::uint16_t port_value, auto configure_fn) : port{port_value}
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

    aevox::App    app{aevox::AppConfig{
           .executor = {.thread_count = 2, .drain_timeout = std::chrono::seconds{2}}}};
    std::uint16_t port;
    std::jthread  thread;
};

void register_static_files(aevox::App& app, const std::filesystem::path& root)
{
    auto middleware = aevox::middleware::static_files({.root = root, .url_prefix = "/static"});
    REQUIRE(middleware.has_value());
    app.use(std::move(*middleware));
    app.get("/static-file/style.css",
            [](aevox::Request&) { return aevox::Response::ok("fallback"); });
}

} // namespace

TEST_CASE("static files roundtrip - GET existing file returns body and headers",
          "[middleware][static-files][integration]")
{
    TempTree tree;
    tree.write("style.css", "body { color: red; }");
    const auto       port = free_port();
    TestServer const server{port,
                            [&tree](aevox::App& app) { register_static_files(app, tree.root); }};

    const auto response = http_request(port, "GET", "/static/style.css");
    REQUIRE(response.find("HTTP/1.1 200") != std::string::npos);
    REQUIRE(response.find("Content-Type: text/css") != std::string::npos);
    REQUIRE(response.find("Content-Length: 20") != std::string::npos);
    REQUIRE(response.ends_with("body { color: red; }"));
}

TEST_CASE("static files roundtrip - missing file returns 404",
          "[middleware][static-files][integration]")
{
    TempTree         tree;
    const auto       port = free_port();
    TestServer const server{port,
                            [&tree](aevox::App& app) { register_static_files(app, tree.root); }};

    const auto response = http_request(port, "GET", "/static/missing.css");
    REQUIRE(response.find("HTTP/1.1 404") != std::string::npos);
}

TEST_CASE("static files roundtrip - encoded traversal returns 403",
          "[middleware][static-files][integration]")
{
    TempTree         tree;
    const auto       port = free_port();
    TestServer const server{port,
                            [&tree](aevox::App& app) { register_static_files(app, tree.root); }};

    const auto response = http_request(port, "GET", "/static/%2e%2e/secret.txt");
    REQUIRE(response.find("HTTP/1.1 403") != std::string::npos);
}

TEST_CASE("static files roundtrip - HEAD existing file returns headers only",
          "[middleware][static-files][integration]")
{
    TempTree tree;
    tree.write("style.css", "body");
    const auto       port = free_port();
    TestServer const server{port,
                            [&tree](aevox::App& app) { register_static_files(app, tree.root); }};

    const auto response = http_request(port, "HEAD", "/static/style.css");
    REQUIRE(response.find("HTTP/1.1 200") != std::string::npos);
    REQUIRE(response.find("Content-Type: text/css") != std::string::npos);
    REQUIRE(response.find("Content-Length: 4") != std::string::npos);
    REQUIRE(response.ends_with("\r\n\r\n"));
}

TEST_CASE("static files roundtrip - partial prefix reaches fallback route",
          "[middleware][static-files][integration]")
{
    TempTree tree;
    tree.write("style.css", "body");
    const auto       port = free_port();
    TestServer const server{port,
                            [&tree](aevox::App& app) { register_static_files(app, tree.root); }};

    const auto response = http_request(port, "GET", "/static-file/style.css");
    REQUIRE(response.find("HTTP/1.1 200") != std::string::npos);
    REQUIRE(response.ends_with("fallback"));
}
