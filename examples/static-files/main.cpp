// examples/static-files/main.cpp
//
// Demonstrates serving application-owned assets with
// aevox::middleware::static_files().
//
// Build:
//   cmake --build --preset default --target static-files-example
//
// Run:
//   ./build/debug/examples/static-files/static-files-example
//   # then in another terminal:
//   curl http://localhost:8080/health
//   curl http://localhost:8080/assets/
//   curl http://localhost:8080/assets/app.css

#include <aevox/app.hpp>
#include <aevox/middleware/static_files.hpp>

#include <filesystem>
#include <format>
#include <iostream>
#include <utility>

int main()
{
    aevox::App app;

    auto assets = aevox::middleware::static_files({
        .root       = std::filesystem::path{AEVOX_STATIC_FILES_EXAMPLE_PUBLIC_DIR},
        .url_prefix = "/assets",
        .index_file = "index.html",
    });

    if (!assets) {
        std::cerr << std::format("[static-files-example] static file config error: {}\n",
                                 aevox::middleware::to_string(assets.error()));
        return 1;
    }

    app.use(std::move(*assets));

    app.get("/", [](aevox::Request&) {
        return aevox::Response::ok("Open http://localhost:8080/assets/ for the static page.");
    });

    app.get("/health", [](aevox::Request&) { return aevox::Response::ok("ok"); });

    std::cout << "[static-files-example] listening on http://localhost:8080\n";
    std::cout << "  Static root: " << AEVOX_STATIC_FILES_EXAMPLE_PUBLIC_DIR << "\n";
    std::cout << "  Try:\n";
    std::cout << "    GET /assets/\n";
    std::cout << "    GET /assets/app.css\n";
    std::cout << "    GET /health\n";

    app.listen(8080);
}
