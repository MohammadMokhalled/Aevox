# CMakeLists.txt — Aevox project root
#
# Declares the top-level project, sets the C++23 baseline, compiler flags,
# and wires subdirectories. This file owns enable_testing() so that CTest
# works from the root build directory.
#
# Requires: CMake 3.27+, Ninja (non-MSVC) or Visual Studio 17 2022 (MSVC),
#           vcpkg toolchain via CMakePresets.json or -DCMAKE_TOOLCHAIN_FILE=

cmake_minimum_required(VERSION 3.27)

project(aevox
    VERSION 0.1.0
    DESCRIPTION "A modern C++23 high-performance web framework"
    HOMEPAGE_URL "https://github.com/aevox/aevox"
    LANGUAGES CXX
)

# ── Language standard ─────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)   # PRD §6.1 — strict standard, no GNU extensions

# Guard: if any subdirectory re-enables extensions, fail loudly.
if(CMAKE_CXX_EXTENSIONS)
    message(FATAL_ERROR
        "CMAKE_CXX_EXTENSIONS must be OFF. "
        "PRD §6.1 violation: GNU extensions are prohibited.")
endif()

# ── vcpkg toolchain notice ────────────────────────────────────────────────────
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    message(WARNING
        "CMAKE_TOOLCHAIN_FILE is not set. "
        "Use --preset <name> or pass "
        "-DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake")
endif()

# ── Compiler flags ────────────────────────────────────────────────────────────
# Applied PRIVATELY to aevox targets only — never forced onto user code or deps.
if(MSVC)
    set(AEVOX_COMPILE_OPTIONS
        /W4         # highest practical warning level
        /WX         # warnings as errors
        /permissive-  # strict conformance mode
        /utf-8      # treat source as UTF-8 (matches Linux/macOS default)
    )
else()
    set(AEVOX_COMPILE_OPTIONS
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

# Export as cache variable so subdirectories can reference it.
set(AEVOX_COMPILE_OPTIONS "${AEVOX_COMPILE_OPTIONS}" CACHE INTERNAL
    "Compiler flags applied privately to all aevox targets")

# ── Testing ───────────────────────────────────────────────────────────────────
# enable_testing() must live here (root) so that `ctest` invoked from the root
# build directory discovers all registered tests across all subdirectories.
enable_testing()

# ── Subdirectories ────────────────────────────────────────────────────────────
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(examples EXCLUDE_FROM_ALL)  # not built by default
