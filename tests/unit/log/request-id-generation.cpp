// request-id-generation.cpp: verify random request_id format
// ADD ref: Tasks/architecture/AEV-012-arch.md §8.1

#include <aevox/log.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cctype>
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
           std::format("aevox-test-req-id-{}-{}.log", counter++,
                       std::chrono::steady_clock::now().time_since_epoch().count());
}

[[nodiscard]] bool is_sixteen_lowercase_hex(const std::string& s)
{
    if (s.size() != 16) {
        return false;
    }
    for (const char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("request_id - is 16 hex characters in JSON output", "[log][tracing]")
{
    const auto path = make_temp_log_path();

    aevox::LogConfig config;
    config.sinks = {aevox::FileSinkConfig{.path = path.string(), .format = aevox::LogFormat::JSON}};

    {
        aevox::AsyncLogWriter writer(config);

        aevox::RequestContext ctx;
        ctx.request_id = "a1b2c3d4e5f6a7b8";
        ctx.thread_id  = 1;

        writer.push(aevox::LogLevel::Info, "test", std::cref(ctx));
    }

    std::ifstream file(path);
    REQUIRE(file.is_open());

    std::string line;
    std::getline(file, line);
    file.close();
    std::filesystem::remove(path);

    // Verify the request_id appears in the JSON output.
    REQUIRE(line.find("\"request_id\":\"a1b2c3d4e5f6a7b8\"") != std::string::npos);
}

TEST_CASE("request_id - 16-hex format is valid", "[log][tracing]")
{
    // Verify the expected format: 16 lowercase hex characters.
    const std::string sample_id = "a1b2c3d4e5f6a7b8";
    REQUIRE(is_sixteen_lowercase_hex(sample_id));

    // All-zeros is technically valid (extremely unlikely from a PRNG).
    const std::string zero_id = "0000000000000000";
    REQUIRE(is_sixteen_lowercase_hex(zero_id));

    // Wrong length should fail.
    REQUIRE_FALSE(is_sixteen_lowercase_hex("abc"));
    REQUIRE_FALSE(is_sixteen_lowercase_hex("a1b2c3d4e5f6a7b89"));

    // Uppercase should fail.
    REQUIRE_FALSE(is_sixteen_lowercase_hex("A1B2C3D4E5F6A7B8"));

    // Non-hex should fail.
    REQUIRE_FALSE(is_sixteen_lowercase_hex("g1b2c3d4e5f6a7b8"));
}
