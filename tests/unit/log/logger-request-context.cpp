// logger-request-context.cpp: verify AEV-029 request id and context extraction
// ADD ref: Tasks/architecture/AEV-029-arch.md §8

#include <aevox/log.hpp>
#include <aevox/request.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "http/http_parser.hpp"
#include "http/request_impl.hpp"
#include "log/log_writer.hpp"

namespace {

[[nodiscard]] aevox::Request make_request()
{
    const std::string raw = "GET /ctx HTTP/1.1\r\nHost: localhost\r\n\r\n";

    std::vector<std::byte> buf;
    buf.reserve(raw.size());
    for (const char ch : raw) {
        buf.push_back(static_cast<std::byte>(ch));
    }

    aevox::detail::ParsedRequest parsed;
    parsed.method     = "GET";
    parsed.target     = "/ctx";
    parsed.keep_alive = false;
    return aevox::make_request_from_impl(std::move(buf), std::move(parsed));
}

[[nodiscard]] std::filesystem::path make_temp_log_path()
{
    static int counter{0};
    return std::filesystem::temp_directory_path() /
           std::format("aevox-aev029-unit-{}-{}.log", counter++,
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

TEST_CASE("AEV-029: request id is stable for request lifetime", "[log]")
{
    auto req  = make_request();
    auto impl = aevox::get_mutable_request_impl(req);
    REQUIRE(impl.has_value());
    impl->get().set_request_id("0000000000000042");

    REQUIRE(req.id() == "0000000000000042");
    REQUIRE(req.id() == "0000000000000042");
}

TEST_CASE("AEV-029: request context includes trace fields only when traceparent is valid", "[log]")
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.destination = aevox::LogDestination::File;
    config.file_path   = path.string();

    auto writer = aevox::detail::LogWriter::create(config);
    REQUIRE(writer.has_value());
    aevox::detail::install_log_writer(*writer);

    auto req  = make_request();
    auto impl = aevox::get_mutable_request_impl(req);
    REQUIRE(impl.has_value());
    impl->get().set_request_id("0000000000000042");
    impl->get().set_trace_id("4bf92f3577b34da6a3ce929d0e0e4736");
    impl->get().set_span_id("00f067aa0ba902b7");

    aevox::log::info(req, "traced");
    REQUIRE(aevox::log::flush().has_value());
    aevox::detail::reset_log_writer();
    writer->reset();

    std::ifstream file{path};
    std::string   content{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::filesystem::remove(path);

    REQUIRE(content.find("\"request_id\":\"0000000000000042\"") != std::string::npos);
    REQUIRE(content.find("\"trace_id\":\"4bf92f3577b34da6a3ce929d0e0e4736\"") != std::string::npos);
    REQUIRE(content.find("\"span_id\":\"00f067aa0ba902b7\"") != std::string::npos);
}

TEST_CASE("AEV-029: global log record omits request fields", "[log]")
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.destination = aevox::LogDestination::File;
    config.file_path   = path.string();

    auto writer = aevox::detail::LogWriter::create(config);
    REQUIRE(writer.has_value());
    aevox::detail::install_log_writer(*writer);

    aevox::log::info("global");
    REQUIRE(aevox::log::flush().has_value());
    aevox::detail::reset_log_writer();
    writer->reset();

    std::ifstream file{path};
    std::string   content{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    std::filesystem::remove(path);

    REQUIRE(content.find("\"message\":\"global\"") != std::string::npos);
    REQUIRE(content.find("\"request_id\"") == std::string::npos);
    REQUIRE(content.find("\"trace_id\"") == std::string::npos);
}
