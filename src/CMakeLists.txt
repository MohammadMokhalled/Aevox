# src/CMakeLists.txt
#
# Declares the `aevox_core` compiled static library target.
# Populated incrementally by subsequent tasks.
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
find_package(llhttp CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

# ── JSON backend selection ────────────────────────────────────────────────────
# Default backend is glaze. To use a custom backend:
#   cmake ... -DAEVOX_JSON_BACKEND=custom
# Custom backends must satisfy aevox::JsonBackend — see include/aevox/json_backend.hpp.
set(AEVOX_JSON_BACKEND "glaze" CACHE STRING
    "JSON backend to compile into aevox_core. Supported values: glaze | custom.")

if(AEVOX_JSON_BACKEND STREQUAL "glaze")
    find_package(glaze CONFIG REQUIRED)
elseif(AEVOX_JSON_BACKEND STREQUAL "custom")
    message(STATUS
        "aevox: AEVOX_JSON_BACKEND=custom. "
        "Define AEVOX_JSON_BACKEND_CUSTOM and provide a type satisfying "
        "aevox::JsonBackend in your build. See include/aevox/json_backend.hpp.")
else()
    message(FATAL_ERROR
        "AEVOX_JSON_BACKEND='${AEVOX_JSON_BACKEND}' is not a recognized value. "
        "Supported values: glaze | custom.")
endif()

add_subdirectory(http)    # aevox_http OBJECT library (HttpParser + llhttp)
add_subdirectory(router)  # aevox_router OBJECT library (Router + App)
add_subdirectory(plugins) # optional first-party plugin targets

add_library(aevox_core STATIC
    error.cpp                       # Shared public error taxonomy helpers
    net/asio_executor.cpp          # Asio-backed executor + accept loop
    net/executor_context.cpp       # thread_local executor bridge definitions
    net/asio_tcp_stream.cpp        # TcpStream pimpl implementation
    net/websocket_handshake.cpp    # RFC 6455 handshake validation and accept-key
    net/websocket_frame.cpp        # RFC 6455 frame parser and emitter
    net/websocket_session.cpp      # Asio-backed WebSocket session (read loop, strand)
    net/topic_bus.cpp              # In-process pub/sub topic registry
    net/websocket.cpp              # WebSocket public handle out-of-line members
    config/toml_loader.cpp         # TOML config file loading (toml++ confined here)
    config/config_impl.cpp         # to_string(ConfigError) implementation
    json/json_error.cpp            # JsonError::message() — out-of-line to prevent false-positive analyser hits
    log/format.cpp                 # Dependency-free JSON and pretty log formatters
    log/log_writer.cpp             # Bounded async log writer and destination dispatch
    log/logger.cpp                 # Public aevox::log functions and global writer handle
    log/middleware_logger.cpp      # aevox::middleware::logger() factory
    $<TARGET_OBJECTS:aevox_http>
    $<TARGET_OBJECTS:aevox_router>
)

target_include_directories(aevox_core
    PUBLIC
        # Build tree: resolve to <project-root>/include
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        # Install tree: resolved by install(EXPORT ...) — configured by a future task
        $<INSTALL_INTERFACE:include>
    PRIVATE
        # Internal headers (src/config/, src/http/, src/net/, src/router/) are
        # included with paths relative to src/ (e.g. "config/toml_loader.hpp").
        ${CMAKE_CURRENT_SOURCE_DIR}
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
        ASIO_STANDALONE          # Use standalone Asio, not Boost.Asio
        ASIO_NO_DEPRECATED       # Eliminate the deprecated Asio API surface
)

# JSON backend compile definition — set when glaze backend is active.
# PRIVATE: the preprocessor guard in request_impl.hpp / response_impl.hpp gates
# the glaze include. Must not be PUBLIC (would expose the define to user code).
if(AEVOX_JSON_BACKEND STREQUAL "glaze")
    target_compile_definitions(aevox_core
        PRIVATE AEVOX_JSON_BACKEND_GLAZE
    )
endif()

# Asio link — PRIVATE: keeps asio:: include paths out of aevox_core consumers.
# llhttp link — PRIVATE: llhttp types must not appear in public headers (ADR compliance).
# tomlplusplus link — PRIVATE: TOML types are confined to src/config/toml_loader.cpp only.
# Note: OBJECT library (aevox_http) does not propagate its PRIVATE deps; link here directly.
target_link_libraries(aevox_core
    PRIVATE
        asio::asio
        llhttp::llhttp_static
        tomlplusplus::tomlplusplus
)

# glaze link — PRIVATE: glaze is header-only; link provides include dirs only.
# No glaze types may appear in public headers (CLAUDE.md §3.2).
# Kept in the same if() guard as find_package(glaze) and target_compile_definitions
# to ensure all three glaze-conditional operations are consistent and auditable together.
if(AEVOX_JSON_BACKEND STREQUAL "glaze")
    target_link_libraries(aevox_core PRIVATE glaze::glaze)
endif()
