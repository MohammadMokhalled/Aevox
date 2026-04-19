# hello-world

The v0.1 milestone example. Demonstrates the complete Aevox public API in a
single file: static routes, named path parameters, and clean SIGINT shutdown.

## Build

```bash
export VCPKG_ROOT=$HOME/vcpkg
cmake --preset default
cmake --build --preset default --target hello-world
```

The binary is written to `build/debug/examples/hello-world/hello-world`.

## Run

```bash
./build/debug/examples/hello-world/hello-world
```

The server listens on port 8080. Stop it with `Ctrl-Z`.

## Routes

| Method | Path | Response |
|---|---|---|
| GET | `/` | `200 Hello, World!` |
| GET | `/hello/{name}` | `200 Hello, <name>!` |
| GET | `/health` | `200 ok` |
| — | anything else | `404 Not Found` |

## Test manually

```bash
curl http://localhost:8080/
curl http://localhost:8080/hello/Alice
curl http://localhost:8080/health
curl http://localhost:8080/missing
```

## Source

```cpp
#include <aevox/app.hpp>

#include <format>
#include <string_view>

int main()
{
    aevox::App app;

    app.get("/", [](aevox::Request&) { return aevox::Response::ok("Hello, World!"); });

    app.get("/hello/{name}", [](aevox::Request& req) {
        auto name = req.param<std::string_view>("name").value_or("stranger");
        return aevox::Response::ok(std::format("Hello, {}!", name));
    });

    app.get("/health", [](aevox::Request&) { return aevox::Response::ok("ok"); });

    app.listen(8080);
}
```
