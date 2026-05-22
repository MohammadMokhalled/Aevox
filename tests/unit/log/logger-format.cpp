// logger-format.cpp: verify AEV-029 dependency-free log formatters
// ADD ref: Tasks/architecture/AEV-029-arch.md §8

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>

#include "log/format.hpp"
#include "log/log_record.hpp"

TEST_CASE("AEV-029: JSON formatter emits global fields", "[log]")
{
    const aevox::detail::LogRecord record{
        .timestamp   = std::chrono::system_clock::time_point{std::chrono::seconds{1}},
        .level       = aevox::LogLevel::Info,
        .message     = "server started",
        .request     = std::nullopt,
        .status_code = std::nullopt,
        .duration    = std::nullopt,
    };

    const auto line = aevox::detail::format_json(record);

    REQUIRE(line.find("\"timestamp\":\"1970-01-01T00:00:01.000000Z\"") != std::string::npos);
    REQUIRE(line.find("\"level\":\"INFO\"") != std::string::npos);
    REQUIRE(line.find("\"message\":\"server started\"") != std::string::npos);
    REQUIRE(line.find("\"request_id\"") == std::string::npos);
}

TEST_CASE("AEV-029: JSON formatter emits request fields when present", "[log]")
{
    const aevox::detail::LogRecord record{
        .timestamp   = std::chrono::system_clock::time_point{std::chrono::seconds{1}},
        .level       = aevox::LogLevel::Warn,
        .message     = "GET /slow -> 200 in 501000us",
        .request     = aevox::detail::LogRequestFields{.request_id = "0000000000000001",
                                                       .method     = "GET",
                                                       .path       = "/slow",
                                                       .trace_id   = "4bf92f3577b34da6a3ce929d0e0e4736",
                                                       .span_id    = "00f067aa0ba902b7"},
        .status_code = 200,
        .duration    = std::chrono::microseconds{501000},
    };

    const auto line = aevox::detail::format_json(record);

    REQUIRE(line.find("\"request_id\":\"0000000000000001\"") != std::string::npos);
    REQUIRE(line.find("\"method\":\"GET\"") != std::string::npos);
    REQUIRE(line.find("\"path\":\"/slow\"") != std::string::npos);
    REQUIRE(line.find("\"status\":200") != std::string::npos);
    REQUIRE(line.find("\"duration_us\":501000") != std::string::npos);
    REQUIRE(line.find("\"trace_id\":\"4bf92f3577b34da6a3ce929d0e0e4736\"") != std::string::npos);
    REQUIRE(line.find("\"span_id\":\"00f067aa0ba902b7\"") != std::string::npos);
}

TEST_CASE("AEV-029: JSON formatter escapes quotes backslashes and control characters", "[log]")
{
    const aevox::detail::LogRecord record{
        .timestamp   = std::chrono::system_clock::time_point{std::chrono::seconds{1}},
        .level       = aevox::LogLevel::Error,
        .message     = "quote \" slash \\ newline\n tab\t",
        .request     = std::nullopt,
        .status_code = std::nullopt,
        .duration    = std::nullopt,
    };

    const auto line = aevox::detail::format_json(record);

    REQUIRE(line.find("quote \\\" slash \\\\ newline\\n tab\\t") != std::string::npos);
}

TEST_CASE("AEV-029: pretty formatter emits one line without JSON syntax", "[log]")
{
    const aevox::detail::LogRecord record{
        .timestamp   = std::chrono::system_clock::time_point{std::chrono::seconds{1}},
        .level       = aevox::LogLevel::Info,
        .message     = "ok",
        .request     = aevox::detail::LogRequestFields{.request_id = "0000000000000001",
                                                       .method     = "GET",
                                                       .path       = "/",
                                                       .trace_id   = "",
                                                       .span_id    = ""},
        .status_code = 200,
        .duration    = std::chrono::microseconds{42},
    };

    const auto line = aevox::detail::format_pretty(record);

    REQUIRE(line.find('{') == std::string::npos);
    REQUIRE(line.find("INFO req=0000000000000001 GET / status=200 dur_us=42 ok") !=
            std::string::npos);
}
