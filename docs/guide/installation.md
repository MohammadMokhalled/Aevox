# Installation

This page covers everything you need to build Aevox from source: prerequisites, cloning, configuring, building, and verifying with the test suite.

## Requirements

| Tool | Minimum version | Notes |
|---|---|---|
| CMake | 3.27 | Required for preset support and C++23 feature detection |
| GCC | 13 (Linux) | C++23 coroutines and `std::expected` require GCC 13+ |
| MSVC | 2022 / 17.8 (Windows) | Visual Studio 2022 with the C++ workload |
| vcpkg | any recent | Used in manifest mode — version is not pinned |
| C++ standard | C++23 | All Aevox headers require C++23 or later |

macOS and Clang are not supported platforms.

## Clone the Repository

```bash
git clone https://github.com/MohammadMokhalled/Aevox.git
cd Aevox
export VCPKG_ROOT=$HOME/vcpkg   # adjust if vcpkg is elsewhere
```

`VCPKG_ROOT` must be set before CMake runs. Aevox uses vcpkg manifest mode: `vcpkg.json` in the repo root lists all dependencies, and CMake installs them automatically during the configure step.

## Configure

Choose the preset that matches your platform and build type.

=== "Linux"
    ```bash
    cmake --preset default
    ```

=== "Windows"
    ```bash
    cmake --preset windows-msvc-debug
    ```

=== "Release"
    ```bash
    cmake --preset release
    ```

The configure step downloads and builds all vcpkg dependencies (Asio, llhttp, Catch2, nanobench). This takes a few minutes on the first run. Subsequent runs are cached.

## Build

```bash
cmake --build build/debug
```

For a release build:

```bash
cmake --build build/release
```

## Verify

Run the full test suite to confirm the build is correct:

```bash
ctest --test-dir build/debug --output-on-failure
```

All tests should pass. The suite includes unit tests, integration tests (real loopback sockets), and benchmarks.

## CMake Integration

Aevox does not yet ship an install target or a find-package config. The recommended way to use Aevox as a dependency today is via vcpkg. Add the repository as a vcpkg port or build from source and include the build directory directly.

When a proper install target is added in a future release, the integration will look like:

```cmake
find_package(aevox REQUIRED)

add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE aevox::aevox)
target_compile_features(my_server PRIVATE cxx_std_23)
```

Until then, use the source-tree build and link `aevox_core` directly from the build output.

## See Also

- [First HTTP Server](first-http-server.md) — write your first working HTTP server after installation
- [Getting Started](../getting-started.md) — a fast-path guide covering a TCP echo server in minutes
