# Static Files

> Middleware for serving read-only application assets from a configured directory.

**Header:** `#include <aevox/middleware/static_files.hpp>`

---

## Overview

Static file middleware maps an application-owned filesystem directory to a public URL prefix. The filesystem root is private server-side state; the URL prefix is the path clients request over HTTP.

Aevox never infers static assets from its own install location. If Aevox is installed through vcpkg or under `/usr`, that only affects headers and libraries. Your application still chooses where its `public/` assets live.

## Quick Start

```cpp
#include <aevox/app.hpp>
#include <aevox/middleware/static_files.hpp>

#include <filesystem>
#include <iostream>
#include <utility>

int main()
{
    aevox::App app;

    auto assets = aevox::middleware::static_files({
        .root = std::filesystem::current_path() / "public",
        .url_prefix = "/static",
        .index_file = "index.html",
    });

    if (!assets) {
        std::cerr << aevox::middleware::to_string(assets.error()) << "\n";
        return 1;
    }

    app.use(std::move(*assets));
    app.listen(8080);
}
```

With that configuration, a request for `/static/app.css` reads `public/app.css`.

## API Reference

#### `StaticFilesConfig`

```cpp
struct StaticFilesConfig
{
    std::filesystem::path root;
    std::string url_prefix{"/static"};
    std::string index_file{};
    std::unordered_map<std::string, std::string> mime_overrides{};
};
```

Configuration for `static_files()`.

| Field | Meaning |
|---|---|
| `root` | Absolute application-owned directory from which files are served. |
| `url_prefix` | Public HTTP prefix that activates the middleware. |
| `index_file` | Optional filename served when a directory is requested. |
| `mime_overrides` | Optional extension-to-MIME map that takes precedence over Aevox defaults. |

`root` must be absolute, must exist, and must name a directory. `url_prefix` must start with `/`; trailing slashes are normalized except for `/` itself.

#### `StaticFilesConfigError`

```cpp
enum class StaticFilesConfigError : std::uint8_t
{
    RootEmpty,
    RootRelative,
    RootNotFound,
    RootNotDirectory,
    InvalidUrlPrefix,
    InvalidIndexFile,
    InvalidMimeOverride,
};
```

Construction-time validation errors for static file middleware.

#### `to_string(StaticFilesConfigError)`

```cpp
[[nodiscard]] std::string_view
to_string(StaticFilesConfigError error) noexcept;
```

Returns a stable human-readable label for a configuration error.

#### `category(StaticFilesConfigError)`

```cpp
[[nodiscard]] aevox::ErrorCategory
category(StaticFilesConfigError error) noexcept;
```

Maps a static-files configuration error to Aevox's shared error taxonomy.

#### `static_files(StaticFilesConfig)`

```cpp
[[nodiscard]] std::expected<aevox::Middleware, StaticFilesConfigError>
static_files(StaticFilesConfig config);
```

Creates middleware that serves `GET` and `HEAD` requests from disk. Non-matching URL prefixes delegate to the next middleware without filesystem access.

Always check the return value:

```cpp
auto assets = aevox::middleware::static_files({
    .root = "/usr/share/example-app/public",
    .url_prefix = "/assets",
});

if (!assets) {
    std::cerr << aevox::middleware::to_string(assets.error()) << "\n";
    return 1;
}

app.use(std::move(*assets));
```

## Error Reference

| Value | Meaning | Typical response |
|---|---|---|
| `RootEmpty` | `root` was empty. | Fix application configuration before startup. |
| `RootRelative` | `root` was not absolute. | Resolve the configured path before calling `static_files()`. |
| `RootNotFound` | `root` could not be canonicalized. | Create the asset directory or correct the path. |
| `RootNotDirectory` | `root` exists but is not a directory. | Point at a directory, not a file. |
| `InvalidUrlPrefix` | `url_prefix` was not a valid URL path prefix. | Use a prefix such as `/static` or `/assets`. |
| `InvalidIndexFile` | `index_file` was not a plain filename. | Use a name such as `index.html`. |
| `InvalidMimeOverride` | A MIME override key or value was malformed. | Use lowercase extension keys such as `.wasm`. |

Request-time outcomes are HTTP responses:

| Request condition | HTTP response |
|---|---|
| Missing file | `404 Not Found` |
| Directory without `index_file` | `404 Not Found` |
| Traversal attempt or symlink escape | `403 Forbidden` |
| Method other than `GET` or `HEAD` | `405 Method Not Allowed` with `Allow: GET, HEAD` |

## Thread Safety

The returned middleware stores immutable configuration and can be invoked concurrently. Treat the served directory as read-only while requests are in flight. Mutable upload directories should use a separate policy outside this middleware.

## Implementation Notes

File reads are synchronous in v0.2 and copy the file into the response body. Use a reverse proxy, CDN, or dedicated asset service for large files or high-volume production asset delivery.

The public header contains no Asio, llhttp, glaze, spdlog, or fmtlib types.

## See Also

- [Static Files Guide](../guide/static-files.md) — deployment layouts and usage patterns
- [Middleware API Reference](middleware.md) — middleware concepts and registration
- [Request and Response API Reference](request-response.md) — response factories and headers
