# Installation

This page covers building, installing, and consuming Aevox on Linux and Windows.

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

## Configure And Build From Source

The default presets build only the Aevox library. Tests and examples are opt-in.

=== "Linux"
    ```bash
    cmake --preset default
    ```

=== "Windows"
    ```bash
    cmake --preset windows-msvc
    ```

=== "Release"
    ```bash
    cmake --preset release
    ```

The configure step downloads and builds vcpkg dependencies. This takes a few minutes on the first
run. Subsequent runs are cached.

=== "Linux"
    ```bash
    cmake --build --preset default
    ```

=== "Windows"
    ```bash
    cmake --build --preset windows-msvc-release
    ```

=== "Release"
    ```bash
    cmake --build --preset release
    ```

## Verify

Use validation presets when you want tests and examples:

=== "Linux"
    ```bash
    cmake --preset default-tests
    cmake --build --preset default-tests
    ctest --preset default-tests
    ```

=== "Windows"
    ```bash
    cmake --preset windows-msvc-tests
    cmake --build --preset windows-msvc-tests-debug
    ctest --preset windows-msvc-tests-debug
    ```

Benchmarks are explicit:

```bash
cmake --preset release-bench
cmake --build --preset release-bench
ctest --preset bench
```

## Install

Install the library into a prefix:

=== "Linux"
    ```bash
    cmake --preset release
    cmake --build --preset release
    cmake --install build/release --prefix "$PWD/build/install/aevox"
    ```

=== "Windows"
    ```powershell
    cmake --preset windows-msvc
    cmake --build --preset windows-msvc-release
    cmake --install build/msvc --config Release --prefix "$PWD/build/install/aevox"
    ```

The install tree contains public headers, the compiled library, and CMake package files under
`lib/cmake/aevox`.

## CMake Integration

Point `CMAKE_PREFIX_PATH` at the install prefix and link the exported target:

```cmake
find_package(aevox REQUIRED)

add_executable(my_server main.cpp)
target_link_libraries(my_server PRIVATE aevox::aevox_core)
target_compile_features(my_server PRIVATE cxx_std_23)
```

For a standalone consumer:

=== "Linux"
    ```bash
    cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/aevox/install
    cmake --build build
    ```

=== "Windows"
    ```powershell
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/path/to/aevox/install
    cmake --build build --config Release
    ```

## See Also

- [First HTTP Server](first-http-server.md) — write your first working HTTP server after installation
- [Getting Started](../getting-started.md) — a fast-path guide covering a TCP echo server in minutes
- [Release Versioning](../contributing/release-versioning.md) — release branch and version format rules
