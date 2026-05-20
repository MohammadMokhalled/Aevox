// Static file middleware unit tests.
// ADD ref: static file middleware test architecture.

#include <aevox/middleware/static_files.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/task.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "http/request_impl.hpp"

namespace {

using NextFn = std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)>;

static std::vector<std::byte> make_buffer(std::string_view value)
{
    std::vector<std::byte> buffer(value.size());
    std::memcpy(buffer.data(), value.data(), value.size());
    return buffer;
}

static aevox::Request make_test_request(aevox::HttpMethod method, std::string_view path)
{
    const auto method_text = aevox::to_string(method);
    const auto raw    = std::string{method_text} + " " + std::string{path} + " HTTP/1.1\r\n\r\n";
    auto       buffer = make_buffer(raw);

    aevox::detail::ParsedRequest parsed;
    parsed.method =
        std::string_view{reinterpret_cast<const char*>(buffer.data()), method_text.size()};
    parsed.target =
        std::string_view{reinterpret_cast<const char*>(buffer.data()) + method_text.size() + 1,
                         path.size()};
    parsed.keep_alive = true;

    return aevox::make_request_from_impl(std::move(buffer), std::move(parsed));
}

template <typename T> static T drive_task(aevox::Task<T> task)
{
    auto inner = task.await_suspend(std::noop_coroutine());
    inner.resume();
    return task.await_resume();
}

struct TempTree
{
    TempTree()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root             = std::filesystem::temp_directory_path() /
               std::filesystem::path{"aevox-static-files-" + std::to_string(stamp)};
        std::filesystem::create_directories(root);
    }

    ~TempTree()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    TempTree(const TempTree&)            = delete;
    TempTree& operator=(const TempTree&) = delete;
    TempTree(TempTree&&)                 = delete;
    TempTree& operator=(TempTree&&)      = delete;

    void write(std::string_view relative, std::string_view body) const
    {
        auto path = root / std::filesystem::path{std::string{relative}};
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file{path, std::ios::binary};
        file << body;
    }

    std::filesystem::path root;
};

static NextFn next_response(std::string body = "next")
{
    return [body = std::move(body)](aevox::Request&) -> aevox::Task<aevox::Response> {
        co_return aevox::Response::ok(body);
    };
}

static aevox::Response call_static_files(aevox::Middleware& middleware, aevox::HttpMethod method,
                                         std::string_view path, NextFn next = next_response())
{
    auto request = make_test_request(method, path);
    return drive_task(middleware(request, std::move(next)));
}

static std::string require_header(const aevox::Response& response, std::string_view name)
{
    auto header = response.get_header(name);
    REQUIRE(header.has_value());
    return std::string{header.value_or({})};
}

} // namespace

TEST_CASE("Static files - config errors expose diagnostic helpers", "[middleware][static-files]")
{
    using aevox::middleware::StaticFilesConfigError;

    struct ExpectedDiagnostic
    {
        StaticFilesConfigError error;
        std::string_view       label;
        aevox::ErrorCategory   category;
    };

    constexpr std::array kExpected{
        ExpectedDiagnostic{.error    = StaticFilesConfigError::RootEmpty,
                           .label    = "root is empty",
                           .category = aevox::ErrorCategory::Validation},
        ExpectedDiagnostic{.error    = StaticFilesConfigError::RootRelative,
                           .label    = "root is relative",
                           .category = aevox::ErrorCategory::Validation},
        ExpectedDiagnostic{.error    = StaticFilesConfigError::RootNotFound,
                           .label    = "root not found",
                           .category = aevox::ErrorCategory::NotFound},
        ExpectedDiagnostic{.error    = StaticFilesConfigError::RootNotDirectory,
                           .label    = "root is not a directory",
                           .category = aevox::ErrorCategory::Validation},
        ExpectedDiagnostic{.error    = StaticFilesConfigError::InvalidUrlPrefix,
                           .label    = "invalid URL prefix",
                           .category = aevox::ErrorCategory::Validation},
        ExpectedDiagnostic{.error    = StaticFilesConfigError::InvalidIndexFile,
                           .label    = "invalid index file",
                           .category = aevox::ErrorCategory::Validation},
        ExpectedDiagnostic{.error    = StaticFilesConfigError::InvalidMimeOverride,
                           .label    = "invalid MIME override",
                           .category = aevox::ErrorCategory::Validation},
    };

    for (const auto& entry : kExpected) {
        CHECK(aevox::middleware::to_string(entry.error) == entry.label);
        CHECK(aevox::middleware::category(entry.error) == entry.category);
    }
}

TEST_CASE("Static files - config rejects empty root", "[middleware][static-files]")
{
    auto result = aevox::middleware::static_files({.root = {}});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == aevox::middleware::StaticFilesConfigError::RootEmpty);
    CHECK(aevox::middleware::category(result.error()) == aevox::ErrorCategory::Validation);
}

TEST_CASE("Static files - config rejects relative root", "[middleware][static-files]")
{
    auto result = aevox::middleware::static_files({.root = "public"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == aevox::middleware::StaticFilesConfigError::RootRelative);
}

TEST_CASE("Static files - config rejects missing root", "[middleware][static-files]")
{
    const TempTree tree;
    auto           result = aevox::middleware::static_files({.root = tree.root / "missing"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == aevox::middleware::StaticFilesConfigError::RootNotFound);
}

TEST_CASE("Static files - config rejects invalid URL prefix", "[middleware][static-files]")
{
    const TempTree tree;
    auto result = aevox::middleware::static_files({.root = tree.root, .url_prefix = "static"});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == aevox::middleware::StaticFilesConfigError::InvalidUrlPrefix);
}

TEST_CASE("Static files - config rejects invalid MIME override", "[middleware][static-files]")
{
    const TempTree tree;
    auto           result = aevox::middleware::static_files(
        {.root = tree.root, .mime_overrides = {{".WASM", "application/wasm"}}});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == aevox::middleware::StaticFilesConfigError::InvalidMimeOverride);
}

TEST_CASE("Static files - serves existing file with correct content-type",
          "[middleware][static-files]")
{
    const TempTree tree;
    tree.write("style.css", "body { color: red; }");
    auto result = aevox::middleware::static_files({.root = tree.root, .url_prefix = "/static"});
    REQUIRE(result.has_value());

    auto response = call_static_files(*result, aevox::HttpMethod::GET, "/static/style.css");
    CHECK(response.status_code() == 200);
    CHECK(response.body_view() == "body { color: red; }");
    CHECK(require_header(response, "Content-Type") == "text/css");
    CHECK(require_header(response, "Content-Length") == "20");
}

TEST_CASE("Static files - MIME override takes precedence", "[middleware][static-files]")
{
    const TempTree tree;
    tree.write("module.wasm", "wasm");
    auto result =
        aevox::middleware::static_files({.root           = tree.root,
                                         .url_prefix     = "/static",
                                         .mime_overrides = {{".wasm", "application/wasm"}}});
    REQUIRE(result.has_value());

    auto response = call_static_files(*result, aevox::HttpMethod::GET, "/static/module.wasm");
    CHECK(response.status_code() == 200);
    CHECK(require_header(response, "Content-Type") == "application/wasm");
}

TEST_CASE("Static files - directory traversal is rejected with 403", "[middleware][static-files]")
{
    const TempTree tree;
    auto result = aevox::middleware::static_files({.root = tree.root, .url_prefix = "/static"});
    REQUIRE(result.has_value());

    auto response =
        call_static_files(*result, aevox::HttpMethod::GET, "/static/../../../etc/passwd");
    CHECK(response.status_code() == 403);
}

TEST_CASE("Static files - encoded directory traversal is rejected with 403",
          "[middleware][static-files]")
{
    const TempTree tree;
    auto result = aevox::middleware::static_files({.root = tree.root, .url_prefix = "/static"});
    REQUIRE(result.has_value());

    auto response = call_static_files(*result, aevox::HttpMethod::GET, "/static/%2e%2e/secret.txt");
    CHECK(response.status_code() == 403);
}

TEST_CASE("Static files - missing file returns 404", "[middleware][static-files]")
{
    const TempTree tree;
    auto result = aevox::middleware::static_files({.root = tree.root, .url_prefix = "/static"});
    REQUIRE(result.has_value());

    auto response = call_static_files(*result, aevox::HttpMethod::GET, "/static/missing.txt");
    CHECK(response.status_code() == 404);
}

TEST_CASE("Static files - directory with index file serves index", "[middleware][static-files]")
{
    const TempTree tree;
    tree.write("docs/index.html", "<h1>Docs</h1>");
    auto result = aevox::middleware::static_files(
        {.root = tree.root, .url_prefix = "/static", .index_file = "index.html"});
    REQUIRE(result.has_value());

    auto response = call_static_files(*result, aevox::HttpMethod::GET, "/static/docs/");
    CHECK(response.status_code() == 200);
    CHECK(response.body_view() == "<h1>Docs</h1>");
    CHECK(require_header(response, "Content-Type") == "text/html");
}

TEST_CASE("Static files - directory without index file returns 404", "[middleware][static-files]")
{
    const TempTree tree;
    std::filesystem::create_directories(tree.root / "docs");
    auto result = aevox::middleware::static_files({.root = tree.root, .url_prefix = "/static"});
    REQUIRE(result.has_value());

    auto response = call_static_files(*result, aevox::HttpMethod::GET, "/static/docs/");
    CHECK(response.status_code() == 404);
}

TEST_CASE("Static files - POST request returns 405", "[middleware][static-files]")
{
    const TempTree tree;
    tree.write("style.css", "body");
    auto result = aevox::middleware::static_files({.root = tree.root, .url_prefix = "/static"});
    REQUIRE(result.has_value());

    auto response = call_static_files(*result, aevox::HttpMethod::POST, "/static/style.css");
    CHECK(response.status_code() == 405);
    CHECK(require_header(response, "Allow") == "GET, HEAD");
}

TEST_CASE("Static files - HEAD request returns headers only", "[middleware][static-files]")
{
    const TempTree tree;
    tree.write("style.css", "body");
    auto result = aevox::middleware::static_files({.root = tree.root, .url_prefix = "/static"});
    REQUIRE(result.has_value());

    auto response = call_static_files(*result, aevox::HttpMethod::HEAD, "/static/style.css");
    CHECK(response.status_code() == 200);
    CHECK(response.body_view().empty());
    CHECK(require_header(response, "Content-Type") == "text/css");
    CHECK(require_header(response, "Content-Length") == "4");
}

TEST_CASE("Static files - non-matching prefix delegates immediately", "[middleware][static-files]")
{
    const TempTree tree;
    auto result = aevox::middleware::static_files({.root = tree.root, .url_prefix = "/static"});
    REQUIRE(result.has_value());

    bool delegated = false;
    auto response =
        call_static_files(*result, aevox::HttpMethod::GET, "/other",
                          [&delegated](aevox::Request&) -> aevox::Task<aevox::Response> {
                              delegated = true;
                              co_return aevox::Response::ok("delegated");
                          });
    CHECK(delegated);
    CHECK(response.status_code() == 200);
    CHECK(response.body_view() == "delegated");
}

TEST_CASE("Static files - segment-aware prefix does not match partial segment",
          "[middleware][static-files]")
{
    const TempTree tree;
    tree.write("style.css", "body");
    auto result = aevox::middleware::static_files({.root = tree.root, .url_prefix = "/static"});
    REQUIRE(result.has_value());

    auto response = call_static_files(*result, aevox::HttpMethod::GET, "/static-file/style.css");
    CHECK(response.status_code() == 200);
    CHECK(response.body_view() == "next");
}

TEST_CASE("Static files - symlink outside root is rejected", "[middleware][static-files]")
{
    const TempTree tree;
    const TempTree outside;
    outside.write("secret.txt", "secret");

    std::error_code ec;
    std::filesystem::create_directory_symlink(outside.root, tree.root / "escape", ec);
    if (ec) {
        SKIP("symlink creation unavailable in this environment");
    }

    auto result = aevox::middleware::static_files({.root = tree.root, .url_prefix = "/static"});
    REQUIRE(result.has_value());

    auto response = call_static_files(*result, aevox::HttpMethod::GET, "/static/escape/secret.txt");
    CHECK(response.status_code() == 403);
}
