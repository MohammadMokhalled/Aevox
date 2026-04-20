# Your First HTTP Server

This page walks you through writing a working HTTP server with three routes, building it, running it, and testing each route from the command line.

## What You Will Build

You will write a server that handles three routes:

- `GET /` — returns a plain "Hello, World!" greeting
- `GET /hello/{name}` — returns a personalised greeting using a path parameter
- `GET /health` — returns "ok" for health checks

Any other request gets an automatic 404 response. The server shuts down cleanly on Ctrl-C.

## Create the File

Create a file `main.cpp` with the following content. This is the complete application — no boilerplate beyond the include.

```cpp
#include <aevox/app.hpp>

#include <format>
#include <string_view>

int main()
{
    aevox::App app;

    // Static route — no path parameters
    app.get("/", [](aevox::Request&) {
        return aevox::Response::ok("Hello, World!");
    });

    // Named path parameter — {name} is captured as std::string_view
    app.get("/hello/{name}", [](aevox::Request& req) {
        auto name = req.param<std::string_view>("name").value_or("stranger");
        return aevox::Response::ok(std::format("Hello, {}!", name));
    });

    // Health check route
    app.get("/health", [](aevox::Request&) {
        return aevox::Response::ok("ok");
    });

    // Blocks here until SIGINT or SIGTERM
    app.listen(8080);
}
```

## Add to CMakeLists.txt

```cmake
add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE aevox_core)
target_compile_features(my_server PRIVATE cxx_std_23)
```

## Build and Run

Configure and build:

```bash
export VCPKG_ROOT=$HOME/vcpkg
cmake --preset default
cmake --build build/debug --target my_server
```

Run the server:

```bash
./build/debug/my_server
```

You should see output similar to:

```
Listening on :8080
```

The server is now accepting connections. Press Ctrl-C to stop it cleanly.

## Test the Routes

In a second terminal, test each route with curl:

```bash
# Root route
curl http://localhost:8080/
# Expected: Hello, World!

# Named parameter route
curl http://localhost:8080/hello/Alice
# Expected: Hello, Alice!

# Health check route
curl http://localhost:8080/health
# Expected: ok

# Unmatched route — automatic 404
curl -i http://localhost:8080/missing
# Expected: HTTP/1.1 404 Not Found
```

## Understanding the Code

Here is what each piece of the API does.

**`aevox::App`** is the top-level server object. It owns the router and the async I/O executor.

```cpp
aevox::App app;
```

**`app.get(pattern, handler)`** registers a handler for GET requests matching the given pattern.

```cpp
app.get("/hello/{name}", handler);
```

**Path parameter syntax `{name}`** captures a URL segment as a named parameter. The captured value is available in the handler via `req.param<T>("name")`.

```cpp
auto name = req.param<std::string_view>("name").value_or("stranger");
```

**`Response::ok(body)`** creates a 200 OK response with a plain-text body.

```cpp
return aevox::Response::ok("Hello, World!");
```

**`app.listen(port)`** binds to the port and blocks until the server is stopped. It installs SIGINT and SIGTERM handlers automatically.

```cpp
app.listen(8080);
```

**Automatic 404** — any request that does not match a registered route receives a 404 Not Found response without any extra code.

## Error Handling

`app.listen()` does not return a value in the simple overload shown above — it blocks until stopped. If you need to handle bind failures (for example, if port 8080 is already in use), use `aevox::App` with the executor directly, or check the error from the underlying executor.

For handlers, `req.param<T>()` returns `std::expected<T, ParamError>`. Always check whether the parameter was found and converted successfully before using it:

```cpp
app.get("/items/{id:int}", [](aevox::Request& req) {
    auto id = req.param<int>("id");
    if (!id) {
        // id.error() is ParamError::NotFound or ParamError::BadConversion
        return aevox::Response::bad_request("invalid item id");
    }
    return aevox::Response::ok(std::format("Item {}", *id));
});
```

Never call `.value()` on the result without first checking `if (id)` or using `.value_or()` — an unexpected absence will terminate the program.

## See Also

- [Routing](routing.md) — all route pattern types, HTTP methods, groups, and match priority
- [Request and Response](request-response.md) — reading headers, body, and parameters; building responses
- [API Reference — Router and App](../api/router.md) — complete App and Router method reference
- [Hello World example](../examples/hello-world.md) — the canonical v0.1 example in the repository
