// log-trace-fields.cpp: verify trace_id and span_id flow through log entries
// ADD ref: Tasks/architecture/AEV-012-arch.md §8.1

#include <aevox/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

#include "log/async_writer.hpp"

namespace {

[[nodiscard]] std::filesystem::path make_temp_log_path()
{
    static std::size_t counter = 0;
    return std::filesystem::temp_directory_path() /
           std::format("aevox-test-trace-fields-{}-{}.log", counter++,
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

TEST_CASE("log entry - trace fields populated when traceparent valid", "[log][tracing]")
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.sinks = {aevox::FileSinkConfig{.path = path.string(), .format = aevox::LogFormat::JSON}};

    {
        aevox::AsyncLogWriter writer(config);

        aevox::RequestContext ctx;
        ctx.request_id = "a1b2c3d4e5f6a7b8";
        ctx.trace_id   = "4bf92f3577b34da6a3ce929d0e0e4736";
        ctx.span_id    = "00f067aa0ba902b7";
        ctx.thread_id  = 42;

        writer.push(aevox::LogLevel::Info, "traced message", std::cref(ctx));
    }

    std::ifstream file(path);
    REQUIRE(file.is_open());

    std::string line;
    std::getline(file, line);
    file.close();
    std::filesystem::remove(path);

    REQUIRE(line.find("\"request_id\":\"a1b2c3d4e5f6a7b8\"") != std::string::npos);
    REQUIRE(line.find("\"trace_id\":\"4bf92f3577b34da6a3ce929d0e0e4736\"") != std::string::npos);
    REQUIRE(line.find("\"span_id\":\"00f067aa0ba902b7\"") != std::string::npos);
    REQUIRE(line.find("\"thread_id\":42") != std::string::npos);
}

TEST_CASE("log entry - trace fields absent when traceparent missing", "[log][tracing]")
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.sinks = {aevox::FileSinkConfig{.path = path.string(), .format = aevox::LogFormat::JSON}};

    {
        aevox::AsyncLogWriter writer(config);

        aevox::RequestContext ctx;
        ctx.request_id = "a1b2c3d4e5f6a7b8";
        ctx.thread_id  = 42;
        // trace_id and span_id left empty

        writer.push(aevox::LogLevel::Info, "untraced message", std::cref(ctx));
    }

    std::ifstream file(path);
    REQUIRE(file.is_open());

    std::string line;
    std::getline(file, line);
    file.close();
    std::filesystem::remove(path);

    REQUIRE(line.find("\"request_id\":\"a1b2c3d4e5f6a7b8\"") != std::string::npos);
    REQUIRE(line.find("\"thread_id\":42") != std::string::npos);
    REQUIRE(line.find("\"trace_id\"") == std::string::npos);
    REQUIRE(line.find("\"span_id\"") == std::string::npos);
}

TEST_CASE("log entry - trace fields absent for global logger (no context)", "[log][tracing]")
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.sinks = {aevox::FileSinkConfig{.path = path.string(), .format = aevox::LogFormat::JSON}};

    {
        aevox::AsyncLogWriter writer(config);

        // Global log — no context.
        writer.push(aevox::LogLevel::Info, "global message");
    }

    std::ifstream file(path);
    REQUIRE(file.is_open());

    std::string line;
    std::getline(file, line);
    file.close();
    std::filesystem::remove(path);

    REQUIRE(line.find("\"request_id\"") == std::string::npos);
    REQUIRE(line.find("\"trace_id\"") == std::string::npos);
    REQUIRE(line.find("\"span_id\"") == std::string::npos);
}
