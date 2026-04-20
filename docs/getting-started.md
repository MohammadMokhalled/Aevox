# Getting Started

This page takes you from zero to a running HTTP server in minutes. You will need CMake, a C++23 compiler, and vcpkg.

---

## Requirements

| Tool | Minimum version |
|---|---|
| CMake | 3.27 |
| GCC | 13 (Linux) |
| MSVC | 2022 / 17.8 (Windows) |
| vcpkg | any recent |
| C++ standard | C++23 |

macOS and Clang are not currently supported.

---

## Installation

Clone the repository and set your vcpkg root:

```bash
git clone https://github.com/MohammadMokhalled/Aevox.git
cd Aevox
export VCPKG_ROOT=$HOME/vcpkg
```

Configure and build:

=== "Linux (GCC 13)"
    ```bash
    cmake --preset default
    cmake --build build/debug
    ```

=== "Windows (MSVC)"
    ```bash
    cmake --preset windows-msvc-debug
    cmake --build --preset windows-msvc-debug
    ```

Verify everything works:

```bash
ctest --test-dir build/debug --output-on-failure
```

---

## Your First HTTP Server

Create `main.cpp`:

```cpp
#include <aevox/app.hpp>

int main()
{
    aevox::App app;

    app.get("/", [](aevox::Request&) {
        return aevox::Response::ok("Hello, World!");
    });

    app.get("/hello/{name}", [](aevox::Request& req) {
        auto name = req.param<std::string_view>("name").value_or("stranger");
        return aevox::Response::ok(std::format("Hello, {}!", name));
    });

    app.get("/health", [](aevox::Request&) {
        return aevox::Response::ok("ok");
    });

    app.listen(8080);
}
```

Add it to your `CMakeLists.txt`:

```cmake
add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE aevox_core)
target_compile_features(my_server PRIVATE cxx_std_23)
```

Build and run:

```bash
cmake --build build/debug --target my_server
./build/debug/my_server
```

Test with curl:

```bash
curl http://localhost:8080/
# Hello, World!

curl http://localhost:8080/hello/Alice
# Hello, Alice!

curl http://localhost:8080/missing
# HTTP/1.1 404 Not Found
```

Press Ctrl-C to stop.

---

## Reading Requests

Handlers receive a `Request&` with everything from the incoming HTTP request:

```cpp
app.get("/items/{id:int}", [](aevox::Request& req) {
    // Typed path parameter — returns std::expected<int, ParamError>
    auto id = req.param<int>("id");
    if (!id) {
        return aevox::Response::bad_request("invalid id");
    }

    auto content_type = req.header("Content-Type"); // std::optional<std::string_view>
    auto body         = req.body();                 // std::string_view

    return aevox::Response::ok(std::format("Item {}", *id));
});
```

---

## Building Responses

Use the factory methods to build responses:

```cpp
aevox::Response::ok("body text")           // 200 OK
aevox::Response::created("body text")      // 201 Created
aevox::Response::not_found("not here")     // 404 Not Found
aevox::Response::bad_request("bad input")  // 400 Bad Request
```

Add headers with the fluent builder:

```cpp
return aevox::Response::ok(json_string)
    .content_type("application/json")
    .header("X-Request-Id", "abc123");
```

---

## Next Steps

| Topic | Where |
|---|---|
| All route pattern types and groups | [User Guide — Routing](guide/routing.md) |
| Full Request and Response API | [User Guide — Request and Response](guide/request-response.md) |
| Async handlers with `co_await` | [User Guide — Async Patterns](guide/async-patterns.md) |
| Error handling with `std::expected` | [User Guide — Error Handling](guide/error-handling.md) |
| Complete API reference | [API Reference](api/index.md) |

---

## See Also

- [User Guide](guide/index.md) — deeper coverage of every feature with worked examples
- [Hello World example](examples/hello-world.md) — the canonical v0.1 example in the repository
