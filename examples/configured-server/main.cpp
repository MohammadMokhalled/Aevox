#include <aevox/app.hpp>
#include <aevox/config.hpp>
#include <aevox/response.hpp>

#include <format>
#include <iostream>
#include <string_view>

// configured-server — demonstrates App::create() with an optional TOML config file.
//
// Usage:
//   ./configured-server                  # pure defaults, no file
//   ./configured-server aevox.toml       # merge file values over defaults
//
// Any field omitted from the file retains its compiled-in default.
// A missing or malformed file is reported and the process exits cleanly.

int main(int argc, char* argv[])
{
    // Collect the optional config path from argv[1].
    std::optional<std::string_view> config_path;
    if (argc >= 2)
        config_path = argv[1];

    // App::create() returns std::expected<App, ConfigErrorDetail>.
    // No file → pure defaults. File present → file values merged over defaults.
    auto result = aevox::App::create({}, config_path);

    if (!result) {
        const auto& err = result.error();
        std::cerr << std::format("[configured-server] config error ({}): {}\n",
                                 aevox::to_string(err.code), err.message);
        return 1;
    }

    auto&       app = *result;
    const auto& cfg = app.config();

    std::cout << std::format("[configured-server] listening on {}:{}\n", cfg.host, cfg.port);
    std::cout << std::format("  max_body_size={}  max_read_bytes={}  max_header_count={}\n",
                             cfg.max_body_size, cfg.max_read_bytes, cfg.max_header_count);
    std::cout << std::format("  io_threads={}  cpu_pool_threads={}  drain_timeout={}s\n",
                             cfg.executor.thread_count, cfg.executor.cpu_pool_threads,
                             cfg.executor.drain_timeout.count());

    app.get("/",
            [](aevox::Request&) { return aevox::Response::ok("configured-server is running"); });

    app.get("/config", [&cfg](aevox::Request&) {
        auto body = std::format("port={} host={} max_body_size={} max_read_bytes={} "
                                "max_header_count={} request_timeout={}s "
                                "io_threads={} cpu_pool_threads={} drain_timeout={}s",
                                cfg.port, cfg.host, cfg.max_body_size, cfg.max_read_bytes,
                                cfg.max_header_count, cfg.request_timeout.count(),
                                cfg.executor.thread_count, cfg.executor.cpu_pool_threads,
                                cfg.executor.drain_timeout.count());
        return aevox::Response::ok(body);
    });

    app.listen();
}
