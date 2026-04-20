// tests/integration/config/config-file-roundtrip.cpp
// Integration test: TOML config file is written to the real filesystem,
// App::create() reads it, a live listen/stop cycle completes without error.
// ADD ref: Tasks/architecture/AEV-025-arch.md § Test Architecture

#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/response.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

using namespace std::chrono_literals;

static std::string write_temp_toml(const std::string& content)
{
    std::string path = std::tmpnam(nullptr); // NOLINT: fine in single-threaded test setup
    path += ".toml";
    std::ofstream f{path};
    f << content;
    return path;
}

TEST_CASE("Config integration - file roundtrip with real App listen", "[config][integration]")
{
    // Write a real TOML config: port=0 (OS assigns ephemeral port),
    // short timeouts so the test exits quickly.
    const std::string toml_content = R"(
max_read_bytes = 8192
max_header_count = 50

[executor]
thread_count     = 2
cpu_pool_threads = 0
drain_timeout    = 2
)";
    const auto        path         = write_temp_toml(toml_content);

    // base_config provides the port (0 = ephemeral) and a short request timeout.
    aevox::AppConfig base;
    base.port                   = 0; // ephemeral port
    base.request_timeout        = 2s;
    base.executor.drain_timeout = 2s;

    auto result = aevox::App::create(base, std::string_view{path});
    std::remove(path.c_str());

    REQUIRE(result.has_value());

    auto&       app = *result;
    const auto& cfg = app.config();

    // Verify file values were applied.
    CHECK(cfg.max_read_bytes == 8192);
    CHECK(cfg.max_header_count == 50);
    CHECK(cfg.executor.thread_count == 2);
    CHECK(cfg.executor.cpu_pool_threads == 0);
    CHECK(cfg.executor.drain_timeout == 2s);
    // Base config values are preserved.
    CHECK(cfg.port == 0);

    // Register a trivial handler and perform a listen/stop lifecycle.
    app.get("/", [](aevox::Request&) { return aevox::Response::ok("ok"); });

    bool         listen_returned = false;
    std::jthread server{[&app, &listen_returned] {
        app.listen();
        listen_returned = true;
    }};

    // Give the server a moment to start, then stop it.
    std::this_thread::sleep_for(50ms);
    app.stop();

    server.join();
    CHECK(listen_returned);
}
