// HTTP keep-alive request/response throughput benchmark.
// ADD ref: Tasks/architecture/AEV-015-arch.md §8.2

#define ANKERL_NANOBENCH_IMPLEMENT
#include <aevox/app.hpp>
#include <aevox/log.hpp>
#include <aevox/response.hpp>

#include <asio.hpp>

#include <nanobench.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "support/bench_stats.hpp"

namespace {

constexpr std::string_view kRequest{
    "GET /hello HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n"};
constexpr std::string_view kHeaderEnd{"\r\n\r\n"};
constexpr int              kConnectAttempts{100};

[[nodiscard]] std::uint16_t find_free_port() noexcept
{
    asio::io_context        io_context;
    asio::ip::tcp::acceptor acceptor{io_context};
    asio::error_code        error;

    acceptor.open(asio::ip::tcp::v4(), error);
    if (error) {
        return 0;
    }

    acceptor.bind(asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 0}, error);
    if (error) {
        return 0;
    }

    acceptor.listen(asio::socket_base::max_listen_connections, error);
    if (error) {
        return 0;
    }

    const auto endpoint = acceptor.local_endpoint(error);
    if (error) {
        return 0;
    }

    return endpoint.port();
}

[[nodiscard]] bool write_all(asio::ip::tcp::socket& socket, std::string_view data) noexcept
{
    asio::error_code error;
    const auto       written = asio::write(socket, asio::buffer(data.data(), data.size()), error);
    return !error && written == data.size();
}

[[nodiscard]] std::optional<std::size_t> parse_content_length(std::string_view headers) noexcept
{
    constexpr std::string_view kHeaderName{"Content-Length:"};

    std::size_t cursor = 0;
    while (cursor < headers.size()) {
        const auto line_end = headers.find("\r\n", cursor);
        const auto line =
            headers.substr(cursor, line_end == std::string_view::npos ? std::string_view::npos
                                                                      : line_end - cursor);
        if (line.starts_with(kHeaderName)) {
            auto value = line.substr(kHeaderName.size());
            while (!value.empty() && value.front() == ' ') {
                value.remove_prefix(1);
            }

            std::size_t length{};
            const auto* begin  = value.data();
            const auto* end    = value.data() + value.size();
            const auto  result = std::from_chars(begin, end, length);
            if (result.ec == std::errc{} && result.ptr == end) {
                return length;
            }
            return std::nullopt;
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        cursor = line_end + 2U;
    }

    return std::nullopt;
}

[[nodiscard]] bool response_ready(std::string_view buffer, std::size_t& response_size) noexcept
{
    const auto header_end = buffer.find(kHeaderEnd);
    if (header_end == std::string_view::npos) {
        return false;
    }

    const auto header_size = header_end + kHeaderEnd.size();
    const auto headers     = buffer.substr(0, header_end);
    const auto body_size   = parse_content_length(headers);
    if (!body_size) {
        return false;
    }

    response_size = header_size + *body_size;
    return buffer.size() >= response_size;
}

[[nodiscard]] bool read_one_response(asio::ip::tcp::socket& socket, std::string& buffer) noexcept
{
    std::size_t response_size{};
    while (!response_ready(buffer, response_size)) {
        std::array<char, 4096> chunk{};
        asio::error_code       error;
        const auto             read = socket.read_some(asio::buffer(chunk), error);
        if (error || read == 0U) {
            return false;
        }
        buffer.append(chunk.data(), read);
    }

    const bool ok = buffer.starts_with("HTTP/1.1 200 ");
    buffer.erase(0, response_size);
    return ok;
}

[[nodiscard]] bool connect_with_retry(asio::ip::tcp::socket& socket, std::uint16_t port) noexcept
{
    const auto endpoint = asio::ip::tcp::endpoint{asio::ip::address_v4::loopback(),
                                                  static_cast<unsigned short>(port)};

    for (int attempt = 0; attempt < kConnectAttempts; ++attempt) {
        asio::error_code error;
        socket.connect(endpoint, error);
        if (!error) {
            return true;
        }
        socket.close(error);
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    return false;
}

[[nodiscard]] bool round_trip(asio::ip::tcp::socket& socket, std::string& response_buffer) noexcept
{
    return write_all(socket, kRequest) && read_one_response(socket, response_buffer);
}

} // namespace

int main()
{
    constexpr int         kEpochIterations = 12000;
    constexpr std::size_t kLatencySamples  = 256;

    const auto port = find_free_port();
    if (port == 0U) {
        std::cerr << "HTTP keep-alive benchmark failed to reserve a loopback port\n";
        return 1;
    }

    aevox::LogConfig logging;
    logging.enabled     = false;
    logging.destination = aevox::LogDestination::Disabled;

    aevox::App app({.port = port, .logging = std::move(logging)});
    app.get("/hello", [](aevox::Request&) { return aevox::Response::ok("Hello, World!"); });

    std::jthread server{[&app] { app.listen(); }};

    asio::io_context      io_context;
    asio::ip::tcp::socket socket{io_context};
    if (!connect_with_retry(socket, port)) {
        app.stop();
        server.join();
        std::cerr << "HTTP keep-alive benchmark failed to connect to Aevox app\n";
        return 1;
    }

    std::string response_buffer;
    response_buffer.reserve(4096);

    std::uint64_t throughput_responses{0};
    std::uint64_t failed_responses{0};

    ankerl::nanobench::Bench bench;
    bench.title("HTTP keep-alive throughput")
        .unit("response")
        .minEpochIterations(kEpochIterations)
        .warmup(50);

    const auto throughput_started = std::chrono::steady_clock::now();
    bench.run("http keepalive throughput - hello world", [&] {
        const bool ok = round_trip(socket, response_buffer);
        if (ok) {
            ++throughput_responses;
        }
        else {
            ++failed_responses;
        }
        ankerl::nanobench::doNotOptimizeAway(ok);
    });
    const auto throughput_finished = std::chrono::steady_clock::now();

    std::vector<std::chrono::nanoseconds> latency_samples;
    latency_samples.reserve(kLatencySamples);
    for (std::size_t index = 0; index < kLatencySamples; ++index) {
        const auto started = std::chrono::steady_clock::now();
        const bool ok      = round_trip(socket, response_buffer);
        const auto ended   = std::chrono::steady_clock::now();
        if (!ok) {
            app.stop();
            server.join();
            std::cerr << "HTTP keep-alive benchmark failed during latency sampling\n";
            return 1;
        }
        latency_samples.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(ended - started));
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(throughput_finished -
                                                                              throughput_started);
    const auto rate    = aevox::test_support::operations_per_second(throughput_responses, elapsed);
    const auto summary = aevox::test_support::summarize_latency(latency_samples);

    std::cout << "HTTP keep-alive responses/sec: " << rate << '\n';
    std::cout << "HTTP keep-alive failed responses: " << failed_responses << '\n';
    std::cout << "HTTP keep-alive latency p50: " << summary.p50.count() << " ns\n";
    std::cout << "HTTP keep-alive latency p99: " << summary.p99.count() << " ns\n";
    std::cout << "HTTP keep-alive latency p999: " << summary.p999.count() << " ns\n";

    asio::error_code error;
    socket.close(error);
    app.stop();
    server.join();

    return throughput_responses == 0U || failed_responses > 0U ? 1 : 0;
}
