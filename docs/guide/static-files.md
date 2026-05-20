# Static Files

Static file middleware serves read-only application assets such as CSS, JavaScript, images, and generated documentation from a directory you configure at startup.

## Basic Setup

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
    });

    if (!assets) {
        std::cerr << aevox::middleware::to_string(assets.error()) << "\n";
        return 1;
    }

    app.use(std::move(*assets));
    app.listen(8080);
}
```

If the application starts in `/home/me/site`, a request for `/static/app.css` reads `/home/me/site/public/app.css`.

## Filesystem Root and URL Prefix

The filesystem root and URL prefix are different things.

| Concept | Example | Purpose |
|---|---|---|
| Filesystem root | `/usr/share/example-app/public` | Private directory Aevox reads from disk |
| URL prefix | `/static` | Public HTTP path clients request |

Aevox's install location is not used as the static root. Installing Aevox through vcpkg, under `/usr`, or as a system package only installs the framework. The consuming application owns its own asset directory.

Common layouts:

| Environment | Root path |
|---|---|
| Local development | Absolute path resolved from project-local `public/` |
| Linux package | `/usr/share/<app-name>/public` |
| Container image | `/app/public` |
| Host-managed service | `/srv/<app-name>/public` or `/opt/<app-name>/public` |

## Security Defaults

Static file middleware is secure by default:

| Behavior | Result |
|---|---|
| `..` path segment | `403 Forbidden` |
| Encoded traversal such as `%2e%2e` | `403 Forbidden` |
| Symlink escaping the configured root | `403 Forbidden` |
| Directory request without `index_file` | `404 Not Found` |
| Non-regular file | `404 Not Found` |
| Method other than `GET` or `HEAD` | `405 Method Not Allowed` |

Directory listings are not supported.

## Index Files

Use `index_file` to serve a file when a directory URL is requested:

```cpp
auto docs = aevox::middleware::static_files({
    .root = "/usr/share/example-app/docs",
    .url_prefix = "/docs",
    .index_file = "index.html",
});

if (!docs) {
    std::cerr << aevox::middleware::to_string(docs.error()) << "\n";
    return 1;
}

app.use(std::move(*docs));
```

With this configuration, `/docs/getting-started/` can serve `/usr/share/example-app/docs/getting-started/index.html`.

## MIME Type Overrides

Aevox infers common MIME types from file extensions. Use `mime_overrides` for application-specific extensions:

```cpp
auto assets = aevox::middleware::static_files({
    .root = "/usr/share/example-app/public",
    .url_prefix = "/assets",
    .mime_overrides = {
        {".wasm", "application/wasm"},
        {".map", "application/json"},
    },
});

if (!assets) {
    std::cerr << aevox::middleware::to_string(assets.error()) << "\n";
    return 1;
}

app.use(std::move(*assets));
```

Override keys must be lowercase extensions with a leading dot.

## Deployment Patterns

For API-first services, mount static files under `/static` or `/assets` so API routes remain visually separate:

```cpp
auto assets = aevox::middleware::static_files({
    .root = "/srv/example-api/public",
    .url_prefix = "/assets",
});

if (!assets) {
    std::cerr << aevox::middleware::to_string(assets.error()) << "\n";
    return 1;
}

app.use(std::move(*assets));
```

For website-style applications, mounting at `/` is possible, but route order and precedence must be considered carefully so assets do not hide API routes.

SPA fallback is not part of the static file middleware in v0.2. A request for a missing file returns `404 Not Found`; it does not automatically serve `index.html`.

## Error Responses

Configuration errors are returned before the server starts:

```cpp
auto assets = aevox::middleware::static_files({
    .root = "public",
});

if (!assets) {
    std::cerr << aevox::middleware::to_string(assets.error()) << "\n";
    return 1;
}
```

Request-time errors are returned as normal HTTP responses:

| Request | Response |
|---|---|
| `/static/missing.css` | `404 Not Found` |
| `/static/../secret.txt` | `403 Forbidden` |
| `POST /static/app.css` | `405 Method Not Allowed` |

## See Also

- [Static Files API Reference](../api/static-files.md) — complete symbol and error reference
- [Middleware Guide](middleware.md) — composing middleware with route handlers
- [Request and Response Guide](request-response.md) — response construction and headers
