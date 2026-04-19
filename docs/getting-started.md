# Getting Started

This guide walks you from zero to a working TCP echo server using Aevox's coroutine-based I/O stack.

!!! tip "Start with the Hello World example"
    If you want to write an HTTP server right away, skip to the [Hello World example](examples/hello-world.md). It covers `App`, `Router`, `Request`, and `Response` in one file. This guide goes deeper into the networking layer underneath.

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

### 1. Clone with vcpkg manifest

Aevox uses [vcpkg manifest mode](https://learn.microsoft.com/en-us/vcpkg/consume/manifest-mode). Dependencies are declared in `vcpkg.json` and installed automatically during the CMake configure step.

```bash
git clone https://github.com/MohammadMokhalled/Aevox.git
cd Aevox
export VCPKG_ROOT=$HOME/vcpkg   # or wherever vcpkg is installed
```

### 2. Configure

=== "Linux (GCC 13)"
    ```bash
    cmake --preset default
    ```

=== "Windows (MSVC)"
    ```bash
    cmake --preset windows-msvc-debug
    ```

=== "Release"
    ```bash
    cmake --preset release
    ```

### 3. Build

```bash
cmake --build build/debug
```

### 4. Run the tests

```bash
ctest --test-dir build/debug --output-on-failure
```

All 44 tests should pass (unit + integration + benchmarks).

---

## Your First TCP Echo Server

Create a file `echo.cpp`:

```cpp
#include <aevox/executor.hpp>
#include <aevox/tcp_stream.hpp>
#include <print>

int main() {
    auto ex = aevox::make_executor({
        .thread_count    = 4,    // 4 I/O threads
        .cpu_pool_threads = 0,   // no CPU pool needed for I/O-only work
    });

    auto result = ex->listen(8080,
        [](std::uint64_t conn_id, aevox::TcpStream stream) -> aevox::Task<void> {
            std::println("conn {} connected", conn_id);

            for (;;) {
                auto data = co_await stream.read();

                if (!data) {
                    // EOF or network error — close the connection
                    std::println("conn {} closed: {}", conn_id,
                                 aevox::to_string(data.error()));
                    co_return;
                }

                // Echo the bytes back
                auto write_result = co_await stream.write(*data);
                if (!write_result) co_return;
            }
        });

    if (!result) {
        std::println(stderr, "listen failed: {}", aevox::to_string(result.error()));
        return 1;
    }

    std::println("Listening on :8080 — Ctrl-C to stop");
    ex->run();
}
```

Add it to your `CMakeLists.txt`:

```cmake
find_package(aevox REQUIRED)

add_executable(echo echo.cpp)
target_link_libraries(echo PRIVATE aevox::aevox)
target_compile_features(echo PRIVATE cxx_std_23)
```

Build and run:

```bash
cmake --build build && ./build/echo
```

Test with netcat:

```bash
echo "Hello Aevox" | nc localhost 8080
# output: Hello Aevox
```

---

## Offloading CPU Work

Use `aevox::pool()` to run CPU-bound code on the dedicated CPU thread pool without blocking an I/O thread:

```cpp
#include <aevox/executor.hpp>
#include <aevox/tcp_stream.hpp>
#include <aevox/async.hpp>
#include <vector>

aevox::Task<void> handle(std::uint64_t, aevox::TcpStream stream) {
    auto data = co_await stream.read();
    if (!data) co_return;

    // Compress on a CPU thread — does not block any I/O thread
    auto compressed = co_await aevox::pool([bytes = *data]() {
        return compress(bytes);   // runs on cpu_pool
    });

    co_await stream.write(std::span{compressed});
}
```

---

## Waiting Without Blocking

`aevox::sleep()` suspends the coroutine without occupying a thread:

```cpp
#include <aevox/async.hpp>
using namespace std::chrono_literals;

aevox::Task<void> delayed_hello(aevox::TcpStream stream) {
    co_await aevox::sleep(100ms);
    co_await stream.write(/* "hello" bytes */);
}
```

---

## Concurrent Fan-Out

`aevox::when_all()` runs multiple `Task<T>` concurrently and collects results:

```cpp
aevox::Task<void> parallel_reads(aevox::TcpStream stream_a, aevox::TcpStream stream_b) {
    auto [data_a, data_b] = co_await aevox::when_all(
        stream_a.read(),
        stream_b.read()
    );
    // both reads have completed
}
```

---

## Graceful Shutdown

`stop()` is thread-safe and can be called from a signal handler:

```cpp
#include <csignal>

std::unique_ptr<aevox::Executor> ex;

int main() {
    ex = aevox::make_executor();
    ex->listen(8080, my_handler);

    std::signal(SIGINT, [](int) { ex->stop(); });

    ex->run();  // blocks until stop() is called, then drains in-flight handlers
}
```

The drain timeout (default 30 s) gives in-flight connections time to finish cleanly.

---

## Next Steps

| Topic | Where |
|---|---|
| HTTP server with routing | [Hello World example](examples/hello-world.md) |
| `App` / `Router` / `Request` / `Response` | [API Reference — Router and App](api/router.md) |
| Full `Executor` API | [API Reference — Executor](api/executor.md) |
| `TcpStream` read/write details | [API Reference — TcpStream](api/tcp_stream.md) |
| CPU offload, timers, fan-out | [API Reference — Async Helpers](api/async.md) |
| Coroutine mechanics | [API Reference — Task](api/task.md) |
| Layer diagram and design decisions | [Architecture](architecture/index.md) |
