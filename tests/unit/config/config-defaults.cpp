// tests/unit/config/config-defaults.cpp
// Unit tests for the runtime configuration system.
// Covers: absent-file defaults, file overrides, missing-file error,
//         unrecognised key, invalid-value error, constexpr defaults.
// ADD ref: Tasks/architecture/AEV-025-arch.md § Test Architecture

#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/executor.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>

using namespace std::chrono_literals;

// ── Helpers ──────────────────────────────────────────────────────────────────

namespace {

std::string write_temp_toml(const std::string& content)
{
    auto path = std::filesystem::temp_directory_path() /
                std::format("aevox_test_{}.toml", std::random_device{}());
    std::ofstream f{path};
    f << content;
    return path.string();
}

} // namespace

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_CASE("Config - absent config file applies all defaults", "[config]")
{
    SECTION("no config path - defaults apply")
    {
        auto result = aevox::App::create({}, std::nullopt);
        REQUIRE(result.has_value());
        const auto& cfg = result->config();
        CHECK(cfg.port == aevox::kDefaultPort);
        CHECK(cfg.host == std::string{aevox::kDefaultHost});
        CHECK(cfg.backlog == aevox::kDefaultBacklog);
        CHECK(cfg.max_body_size == aevox::kDefaultMaxBodySize);
        CHECK(cfg.max_read_bytes == aevox::kDefaultMaxReadBytes);
        CHECK(cfg.max_header_count == aevox::kDefaultMaxHeaderCount);
        CHECK(cfg.request_timeout == aevox::kDefaultRequestTimeout);
        CHECK(cfg.executor.thread_count == aevox::kDefaultIoThreadCount);
        CHECK(cfg.executor.cpu_pool_threads == aevox::kDefaultCpuPoolThreads);
        CHECK(cfg.executor.drain_timeout == aevox::kDefaultDrainTimeout);
        CHECK(cfg.logging.enabled);
        CHECK(cfg.logging.level == aevox::LogLevel::Info);
        CHECK(cfg.logging.format == aevox::LogFormat::Json);
        CHECK(cfg.logging.destination == aevox::LogDestination::Stdout);
        CHECK(cfg.logging.queue_capacity == 8192);
    }

    SECTION("empty config path string - defaults apply")
    {
        auto result = aevox::App::create({}, std::string_view{""});
        REQUIRE(result.has_value());
        CHECK(result->config().port == aevox::kDefaultPort);
    }
}

TEST_CASE("Config - present config file overrides specified fields", "[config]")
{
    const std::string toml_content = R"(
port = 9090

[executor]
thread_count = 2
drain_timeout = 5
)";
    const auto        path         = write_temp_toml(toml_content);

    auto result = aevox::App::create({}, std::string_view{path});
    std::remove(path.c_str());

    REQUIRE(result.has_value());
    const auto& cfg = result->config();
    CHECK(cfg.port == 9090);
    CHECK(cfg.executor.thread_count == 2);
    CHECK(cfg.executor.drain_timeout == std::chrono::seconds{5});
    // Non-specified fields retain defaults.
    CHECK(cfg.host == std::string{aevox::kDefaultHost});
    CHECK(cfg.max_body_size == aevox::kDefaultMaxBodySize);
    CHECK(cfg.executor.cpu_pool_threads == aevox::kDefaultCpuPoolThreads);
}

TEST_CASE("Config - logging section overrides simplified logger fields", "[config]")
{
    const std::string toml_content = R"(
[logging]
enabled = true
level = "warn"
format = "pretty"
destination = "file"
file_path = "/tmp/aevox.log"
queue_capacity = 128
)";
    const auto        path         = write_temp_toml(toml_content);

    auto result = aevox::App::create({}, std::string_view{path});
    std::remove(path.c_str());

    REQUIRE(result.has_value());
    const auto& logging = result->config().logging;
    CHECK(logging.enabled);
    CHECK(logging.level == aevox::LogLevel::Warn);
    CHECK(logging.format == aevox::LogFormat::Pretty);
    CHECK(logging.destination == aevox::LogDestination::File);
    REQUIRE(logging.file_path.has_value());
    CHECK(*logging.file_path == "/tmp/aevox.log");
    CHECK(logging.queue_capacity == 128);
}

TEST_CASE("Config - missing file returns ConfigError::file_not_found", "[config]")
{
    auto result = aevox::App::create({}, "/nonexistent/path/aevox_test.toml");

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().error_code() == aevox::ConfigError::FileNotFound);
    CHECK_FALSE(result.error().error_message().empty());
    CHECK(aevox::category(result.error()) == aevox::ErrorCategory::NotFound);
    // Must not throw.
}

TEST_CASE("Config - unrecognised key is silently ignored", "[config]")
{
    const std::string toml_content = R"(
unknown_key = 42
port = 7777
)";
    const auto        path         = write_temp_toml(toml_content);

    auto result = aevox::App::create({}, std::string_view{path});
    std::remove(path.c_str());

    // Parsing should succeed — the unknown key is ignored.
    REQUIRE(result.has_value());
    CHECK(result->config().port == 7777);
}

TEST_CASE("Config - invalid value returns ConfigError::invalid_value", "[config]")
{
    SECTION("port = 0 is below valid range")
    {
        const auto path   = write_temp_toml("port = 0\n");
        auto       result = aevox::App::create({}, std::string_view{path});
        std::remove(path.c_str());

        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().error_code() == aevox::ConfigError::InvalidValue);
        CHECK(result.error().error_key() == "port");
        CHECK(aevox::category(result.error()) == aevox::ErrorCategory::Validation);
    }

    SECTION("executor.drain_timeout = 0 is below valid range")
    {
        const auto path   = write_temp_toml("[executor]\ndrain_timeout = 0\n");
        auto       result = aevox::App::create({}, std::string_view{path});
        std::remove(path.c_str());

        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().error_code() == aevox::ConfigError::InvalidValue);
        CHECK(result.error().error_key() == "executor.drain_timeout");
    }

    SECTION("max_header_count = 9999 is above valid range")
    {
        const auto path   = write_temp_toml("max_header_count = 9999\n");
        auto       result = aevox::App::create({}, std::string_view{path});
        std::remove(path.c_str());

        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().error_code() == aevox::ConfigError::InvalidValue);
        CHECK(result.error().error_key() == "max_header_count");
    }
}

TEST_CASE("Config - to_string returns exact implementation strings", "[config]")
{
    // These assertions pin the exact string literals returned by the implementation.
    // A caller who matches on these strings (e.g. for logging or diagnostics) must
    // not be surprised by a silent rename. Any change to config_impl.cpp must also
    // update this test and docs/api/config.md.
    CHECK(aevox::to_string(aevox::ConfigError::FileNotFound) == "file not found");
    CHECK(aevox::to_string(aevox::ConfigError::ParseError) == "TOML parse error");
    CHECK(aevox::to_string(aevox::ConfigError::InvalidValue) == "invalid field value");
    CHECK(aevox::category(aevox::ConfigError::FileNotFound) == aevox::ErrorCategory::NotFound);
    CHECK(aevox::category(aevox::ConfigError::ParseError) == aevox::ErrorCategory::Parse);
    CHECK(aevox::category(aevox::ConfigError::InvalidValue) == aevox::ErrorCategory::Validation);
}

TEST_CASE("Config - constexpr defaults match AppConfig field initialisers", "[config]")
{
    // compile-time assertions — the test body exists to give Catch2 a REQUIRE call.
    // AppConfig::host is std::string — not constexpr-constructible in C++23.
    // The runtime check in "absent config file applies all defaults" covers this field.
    static_assert(aevox::AppConfig{}.port == aevox::kDefaultPort);
    static_assert(aevox::AppConfig{}.backlog == aevox::kDefaultBacklog);
    static_assert(aevox::AppConfig{}.max_body_size == aevox::kDefaultMaxBodySize);
    static_assert(aevox::AppConfig{}.max_read_bytes == aevox::kDefaultMaxReadBytes);
    static_assert(aevox::AppConfig{}.max_header_count == aevox::kDefaultMaxHeaderCount);
    static_assert(aevox::AppConfig{}.request_timeout == aevox::kDefaultRequestTimeout);
    static_assert(aevox::ExecutorConfig{}.thread_count == aevox::kDefaultIoThreadCount);
    static_assert(aevox::ExecutorConfig{}.cpu_pool_threads == aevox::kDefaultCpuPoolThreads);
    static_assert(aevox::ExecutorConfig{}.drain_timeout == aevox::kDefaultDrainTimeout);
    REQUIRE(true); // satisfied above via static_assert
}
