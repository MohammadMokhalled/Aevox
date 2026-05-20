# Static Files

Demonstrates `aevox::middleware::static_files()` with an application-owned `public/` directory bundled beside the example. The filesystem root is configured explicitly at startup and mounted at the public `/assets` URL prefix.

**Source:** [`examples/static-files/main.cpp`](https://github.com/MohammadMokhalled/Aevox/blob/main/examples/static-files/main.cpp)

---

## Build and run

```bash
export VCPKG_ROOT=$HOME/vcpkg
cmake --preset default
cmake --build --preset default --target static-files-example
./build/debug/examples/static-files/static-files-example
```

The server listens on port 8080. Stop it with `Ctrl-C`.

---

## Routes

| Method | Path | Source |
|---|---|---|
| `GET` | `/` | Dynamic Aevox route |
| `GET` | `/health` | Dynamic Aevox route |
| `GET` | `/assets/` | `examples/static-files/public/index.html` |
| `GET` | `/assets/app.css` | `examples/static-files/public/app.css` |

---

## Test it

```bash
curl http://localhost:8080/
curl http://localhost:8080/health
curl http://localhost:8080/assets/
curl http://localhost:8080/assets/app.css
curl -I http://localhost:8080/assets/
```

Traversal attempts are rejected:

```bash
curl -i http://localhost:8080/assets/%2e%2e/CMakeLists.txt
```

---

## Full source

```cpp
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
        .root = std::filesystem::path{AEVOX_STATIC_FILES_EXAMPLE_PUBLIC_DIR},
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

    app.listen(8080);
}
```

---

## What this covers

- **`aevox::middleware::static_files()`** — creates middleware from validated configuration
- **Application-owned root** — the example passes its own `public/` directory explicitly
- **URL prefix mounting** — `/assets/app.css` maps to `public/app.css`
- **Directory index** — `/assets/` serves `public/index.html`
- **Dynamic routes beside static files** — `/` and `/health` still use normal handlers
- **Error handling** — startup checks the `std::expected` result before registering middleware

---

## API reference

| Type | Header | Docs |
|---|---|---|
| `aevox::middleware::static_files()` | `<aevox/middleware/static_files.hpp>` | [Static Files](../api/static-files.md) |
| `aevox::App::use()` | `<aevox/app.hpp>` | [Router and App](../api/router.md) |

---

## See Also

- [User Guide — Static Files](../guide/static-files.md) — deployment layouts and security defaults
- [Middleware Plugin example](middleware-plugin.md) — custom middleware composition
