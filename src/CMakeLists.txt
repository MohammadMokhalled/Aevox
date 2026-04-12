# src/CMakeLists.txt
#
# Declares the `aevox_core` compiled static library target.
# Populated incrementally by subsequent tasks (AEV-001 adds net/, AEV-003 adds http/, etc.)
#
# Requires: vcpkg toolchain (VCPKG_ROOT set, all packages in vcpkg.json resolved).
# Provides: `aevox_core` target for linking by tests, examples, and user applications.
#
# IMPORTANT — Asio boundary enforcement:
#   ASIO_STANDALONE and asio::asio are linked PRIVATELY. This prevents Asio
#   include paths from propagating to consumers of aevox_core, enforcing the
#   architectural invariant that no user code or public header ever sees Asio.
#   Changing these to PUBLIC would silently break the C++29 std::net migration path.

find_package(asio CONFIG REQUIRED)

add_library(aevox_core STATIC
    aevox_placeholder.cpp   # Removed by AEV-001 when src/net/asio_executor.cpp lands
)

target_include_directories(aevox_core
    PUBLIC
        # Build tree: resolve to <project-root>/include
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        # Install tree: resolved by install(EXPORT ...) — configured by a future task
        $<INSTALL_INTERFACE:include>
)

target_compile_features(aevox_core
    PUBLIC cxx_std_23   # Propagates to all consumers — they must also compile as C++23
)

target_compile_options(aevox_core
    PRIVATE ${AEVOX_COMPILE_OPTIONS}   # -Wall -Wextra -Wpedantic -Werror (or MSVC equiv)
)

# Asio compile definitions — PRIVATE: implementation detail of src/net/ only.
# Do NOT change to PUBLIC without Architect review (ADR-1 violation).
target_compile_definitions(aevox_core
    PRIVATE
        ASIO_STANDALONE      # Use standalone Asio, not Boost.Asio
        ASIO_NO_DEPRECATED   # Eliminate the deprecated Asio API surface
)

# Asio link — PRIVATE: keeps asio:: include paths out of aevox_core consumers.
target_link_libraries(aevox_core
    PRIVATE asio::asio
)
