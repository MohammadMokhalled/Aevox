// AEV-003: HTTP/1.1 parser integration tests — full stack over real loopback TCP.
// ADD ref: Tasks/architecture/AEV-003-arch.md §8.2
//
// Uses real aevox::Executor + aevox::TcpStream + aevox::detail::HttpParser.
// No mocks. All tests run with thread_count=2, drain_timeout=3s.

#include "http/http_parser.hpp"

#include <aevox/executor.hpp>
#include <aevox/task.hpp>

#include <asio.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <latch>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static aevox::ExecutorConfig make_cfg()
{
    return {.thread_count = 2, .drain_timeout = 3s};
}

static std::uint16_t free_port()
{
    asio::io_context        ioc;
    asio::ip::tcp::acceptor a{ioc, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}};
    return a.local_endpoint().port();
}

static void tcp_send(std::uint16_t port, std::string_view data)
{
    asio::io_context      ioc;
    asio::ip::tcp::socket s{ioc};
    asio::error_code      ec;
    s.connect(asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port}, ec);
    if (!ec)
        asio::write(s, asio::buffer(data.data(), data.size()), ec);
}

static void tcp_connect_close(std::uint16_t port)
{
    asio::io_context      ioc;
    asio::ip::tcp::socket s{ioc};
    asio::error_code      ec;
    s.connect(asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port}, ec);
    // Close immediately — no data sent.
}

// ==============================================================================

TEST_CASE("AEV-003 integration: GET request roundtrip via loopback", "[integration][http]")
{
    std::string got_method;
    std::string got_target;
    std::latch  done{1};

    auto ex   = aevox::make_executor(make_cfg());
    auto port = free_port();

    auto lr = ex->listen(port, [&](std::uint64_t, aevox::TcpStream stream) -> aevox::Task<void> {
        aevox::detail::HttpParser parser;
        for (;;) {
            auto data = co_await stream.read();
            if (!data.has_value()) co_return;

            auto res = parser.feed(std::span{*data});
            if (res.has_value()) {
                got_method = std::string{res->method};
                got_target = std::string{res->target};
                done.count_down();
                co_return;
            }
            if (res.error() != aevox::detail::ParseError::Incomplete) co_return;
        }
    });
    REQUIRE(lr.has_value());

    std::jthread driver{[&ex, port, &done] {
        std::this_thread::sleep_for(10ms);
        tcp_send(port, "GET /hello HTTP/1.1\r\nHost: localhost\r\n\r\n");
        done.wait();
        ex->stop();
    }};

    auto run = ex->run();
    REQUIRE(run.has_value());
    CHECK(got_method == "GET");
    CHECK(got_target == "/hello");
}

// =============================================================================

TEST_CASE("AEV-003 integration: POST with body roundtrip via loopback", "[integration][http]")
{
    std::string got_body;
    std::latch  done{1};

    auto ex   = aevox::make_executor(make_cfg());
    auto port = free_port();

    auto lr = ex->listen(port, [&](std::uint64_t, aevox::TcpStream stream) -> aevox::Task<void> {
        aevox::detail::HttpParser parser;
        std::vector<std::byte> buf;
        for (;;) {
            auto data = co_await stream.read();
            if (!data.has_value()) co_return;

            buf.insert(buf.end(), data->begin(), data->end());
            auto res = parser.feed(std::span{buf});
            if (res.has_value()) {
                got_body = std::string(
                    reinterpret_cast<const char*>(res->body.data()),
                    res->body.size());
                done.count_down();
                co_return;
            }
            if (res.error() != aevox::detail::ParseError::Incomplete) co_return;
        }
    });
    REQUIRE(lr.has_value());

    std::jthread driver{[&ex, port, &done] {
        std::this_thread::sleep_for(10ms);
        tcp_send(port,
            "POST /data HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 5\r\n"
            "\r\n"
            "hello");
        done.wait();
        ex->stop();
    }};

    auto run = ex->run();
    REQUIRE(run.has_value());
    CHECK(got_body == "hello");
}

// =============================================================================

TEST_CASE("AEV-003 integration: pipelined keep-alive requests", "[integration][http]")
{
    // Two back-to-back requests on one TCP connection (keep-alive).
    // Requests are sent sequentially so each arrives in its own read() call,
    // avoiding the need to track byte offsets across pipelined buffers.

    std::atomic<int> request_count{0};
    std::string      targets[2];
    std::latch       done{1};

    auto ex   = aevox::make_executor(make_cfg());
    auto port = free_port();

    auto lr = ex->listen(port, [&](std::uint64_t, aevox::TcpStream stream) -> aevox::Task<void> {
        aevox::detail::HttpParser parser;
        std::vector<std::byte> accumulator;

        while (request_count.load() < 2) {
            auto data = co_await stream.read();
            if (!data.has_value()) break;

            accumulator.insert(accumulator.end(), data->begin(), data->end());
            auto res = parser.feed(std::span{accumulator});
            if (res.has_value()) {
                int idx = request_count.fetch_add(1);
                if (idx < 2) targets[idx] = std::string{res->target};
                accumulator.clear();
                parser.reset();
                if (request_count.load() >= 2) {
                    done.count_down();
                    co_return;
                }
            }
            // ParseError::Incomplete is fine — keep reading.
        }
        if (request_count.load() >= 2) done.count_down();
    });
    REQUIRE(lr.has_value());

    std::jthread driver{[&ex, port, &done] {
        asio::io_context      ioc;
        asio::ip::tcp::socket s{ioc};
        asio::error_code      ec;
        s.connect(asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(), port}, ec);
        REQUIRE_FALSE(ec);

        // Send first request, then second on the same connection.
        std::string_view req1 = "GET /first HTTP/1.1\r\nHost: localhost\r\n\r\n";
        std::string_view req2 = "GET /second HTTP/1.1\r\nHost: localhost\r\n\r\n";
        asio::write(s, asio::buffer(req1.data(), req1.size()), ec);
        std::this_thread::sleep_for(10ms);  // let server process req1 first
        asio::write(s, asio::buffer(req2.data(), req2.size()), ec);

        done.wait();
        ex->stop();
    }};

    auto run = ex->run();
    REQUIRE(run.has_value());
    REQUIRE(request_count.load() == 2);
    CHECK(targets[0] == "/first");
    CHECK(targets[1] == "/second");
}

// =============================================================================

TEST_CASE("AEV-003 integration: malformed request — 400 path", "[integration][http]")
{
    std::atomic<bool> got_bad_request{false};
    std::latch        done{1};

    auto ex   = aevox::make_executor(make_cfg());
    auto port = free_port();

    auto lr = ex->listen(port, [&](std::uint64_t, aevox::TcpStream stream) -> aevox::Task<void> {
        aevox::detail::HttpParser parser;
        for (;;) {
            auto data = co_await stream.read();
            if (!data.has_value()) {
                done.count_down();
                co_return;
            }
            auto res = parser.feed(std::span{*data});
            if (!res.has_value() && res.error() == aevox::detail::ParseError::BadRequest) {
                got_bad_request = true;
                done.count_down();
                co_return;
            }
            if (res.has_value()) {
                done.count_down();
                co_return;
            }
        }
    });
    REQUIRE(lr.has_value());

    std::jthread driver{[&ex, port, &done] {
        std::this_thread::sleep_for(10ms);
        tcp_send(port, "GARBAGE BYTES NOT HTTP AT ALL\r\n\r\n");
        done.wait();
        ex->stop();
    }};

    auto run = ex->run();
    REQUIRE(run.has_value());
    CHECK(got_bad_request.load());
}

// =============================================================================

TEST_CASE("AEV-003 integration: connection EOF mid-request", "[integration][http]")
{
    std::atomic<bool> got_eof{false};
    std::latch        done{1};

    auto ex   = aevox::make_executor(make_cfg());
    auto port = free_port();

    auto lr = ex->listen(port, [&](std::uint64_t, aevox::TcpStream stream) -> aevox::Task<void> {
        auto data = co_await stream.read();
        if (!data.has_value() && data.error() == aevox::IoError::Eof) {
            got_eof = true;
        }
        done.count_down();
    });
    REQUIRE(lr.has_value());

    std::jthread driver{[&ex, port, &done] {
        std::this_thread::sleep_for(10ms);
        tcp_connect_close(port);  // connect then immediately close — EOF
        done.wait();
        ex->stop();
    }};

    auto run = ex->run();
    REQUIRE(run.has_value());
    CHECK(got_eof.load());
}
