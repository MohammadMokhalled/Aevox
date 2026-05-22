// global-vs-request-logger.cpp: verify global omits request fields
// ADD ref: Tasks/architecture/AEV-011-arch.md § Test Architecture

#include <aevox/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

#include "http/request_impl.hpp"
#include "log/async_writer.hpp"
#include "log/log_entry.hpp"

namespace {

[[nodiscard]] std::filesystem::path make_temp_log_path()
{
    static std::size_t counter = 0;
    return std::filesystem::temp_directory_path() /
           std::format("aevox-test-global-req-{}-{}.log", counter++,
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

TEST_CASE("global vs request logger - global omits request_id", "[log]")
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.sinks = {aevox::FileSinkConfig{.path = path.string(), .format = aevox::LogFormat::JSON}};

    {
        aevox::AsyncLogWriter writer(config);

        // Global log — no context.
        writer.push(aevox::LogLevel::Info, "global msg");

        // Request log — with context.
        aevox::RequestContext ctx;
        ctx.request_id = "req-abc";
        ctx.trace_id   = "4bf92f3577b34da6a3ce929d0e0e4736";
        ctx.span_id    = "00f067aa0ba902b7";
        ctx.thread_id  = 99;
        writer.push(aevox::LogLevel::Info, "request msg", std::cref(ctx));
    } // destructor joins drain thread and flushes all entries

    std::ifstream file(path);
    REQUIRE(file.is_open());

    const std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    file.close();
    std::filesystem::remove(path);

    // Global entry omits request_id, trace_id, and span_id.
    const auto global_pos = content.find("global msg");
    REQUIRE(global_pos != std::string::npos);
    const auto next_newline = content.find('\n', global_pos);
    const auto global_end   = next_newline == std::string::npos ? content.size() : next_newline;
    REQUIRE(content.find("\"request_id\"", global_pos) > global_end);
    REQUIRE(content.find("\"trace_id\"", global_pos) > global_end);
    REQUIRE(content.find("\"span_id\"", global_pos) > global_end);

    // Request entry carries all context fields.
    REQUIRE(content.find("request msg") != std::string::npos);
    REQUIRE(content.find("\"request_id\":\"req-abc\"") != std::string::npos);
    REQUIRE(content.find("\"trace_id\":\"4bf92f3577b34da6a3ce929d0e0e4736\"") != std::string::npos);
    REQUIRE(content.find("\"span_id\":\"00f067aa0ba902b7\"") != std::string::npos);
    REQUIRE(content.find("\"thread_id\":99") != std::string::npos);
}
