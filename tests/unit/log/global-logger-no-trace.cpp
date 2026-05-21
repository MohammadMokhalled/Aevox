// global-logger-no-trace.cpp: verify global logger omits trace fields
// ADD ref: Tasks/architecture/AEV-012-arch.md §8.1

#include <aevox/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "log/async_writer.hpp"
#include "log/log_entry.hpp"

namespace {

[[nodiscard]] std::filesystem::path make_temp_log_path()
{
    static std::size_t counter = 0;
    return std::filesystem::temp_directory_path() /
           std::format("aevox-test-global-no-trace-{}-{}.log", counter++,
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace

TEST_CASE("global logger - omits request_id and trace fields", "[log][tracing]")
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.sinks = {aevox::FileSinkConfig{.path = path.string(), .format = aevox::LogFormat::JSON}};

    {
        aevox::AsyncLogWriter writer(config);

        // Global log — no context pointer.
        writer.push(aevox::LogLevel::Info, "global test message");
    }

    std::ifstream file(path);
    REQUIRE(file.is_open());

    std::string line;
    std::getline(file, line);
    file.close();
    std::filesystem::remove(path);

    // Verify the message is present.
    REQUIRE(line.find("\"message\":\"global test message\"") != std::string::npos);

    // Verify no request-scoped fields appear.
    REQUIRE(line.find("\"request_id\"") == std::string::npos);
    REQUIRE(line.find("\"trace_id\"") == std::string::npos);
    REQUIRE(line.find("\"span_id\"") == std::string::npos);
    REQUIRE(line.find("\"thread_id\"") == std::string::npos);
}
