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
    VERSION 0.3.0
    DESCRIPTION "A modern C++23 high-performance web framework"
    HOMEPAGE_URL "https://github.com/aevox/aevox"
    LANGUAGES CXX
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
include(cmake/aevox_version.cmake)

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
    # _WIN32_WINNT=0x0A00: target Windows 10+ APIs; suppresses the SDK's own
    # "please define _WIN32_WINNT" diagnostic that otherwise spills into builds.
    # NOMINMAX: prevent <windows.h> from defining min/max macros, which break
    # std::numeric_limits<T>::max() and similar STL expressions under MSVC.
    add_compile_definitions(
        _WIN32_WINNT=0x0A00
        NOMINMAX
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

# ── Optional first-party plugins ──────────────────────────────────────────────
option(AEVOX_BUILD_TESTS "Build Aevox tests" OFF)
option(AEVOX_BUILD_EXAMPLES "Build Aevox examples" OFF)
option(AEVOX_BUILD_BENCHMARKS "Build Aevox benchmarks" OFF)
option(AEVOX_ENABLE_GRPC "Build the official Aevox gRPC plugin" OFF)

# ── Testing ───────────────────────────────────────────────────────────────────
if(AEVOX_BUILD_TESTS OR AEVOX_BUILD_BENCHMARKS)
    # enable_testing() must live here (root) so that `ctest` invoked from the
    # root build directory discovers all registered tests across subdirectories.
    enable_testing()
endif()

# ── Subdirectories ────────────────────────────────────────────────────────────
add_subdirectory(src)

if(AEVOX_BUILD_TESTS OR AEVOX_BUILD_BENCHMARKS)
    add_subdirectory(tests)
endif()

if(AEVOX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ── Installation ──────────────────────────────────────────────────────────────
install(TARGETS aevox_core
    EXPORT aevox-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/aevox
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN ".gitkeep" EXCLUDE
)

install(EXPORT aevox-targets
    FILE aevox-targets.cmake
    NAMESPACE aevox::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aevox
)

configure_package_config_file(
    cmake/aevox-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/aevox-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aevox
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/aevox-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/aevox-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/aevox-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/aevox
)
