# src/router/CMakeLists.txt
#
# aevox_router — OBJECT library containing the Router trie and App lifecycle.
# Compiled into aevox_core via $<TARGET_OBJECTS:aevox_router>.
#
# Constraint: no Asio types may appear in handler_wrap.hpp (which is transitively
# included by the public include/aevox/router.hpp). This is verified in CI by the
# no-asio-in-public-headers check. See Tasks/architecture/AEV-004-arch.md OI-1.
#
# IMPORTANT — OBJECT library transitive deps:
#   CMake does not propagate PRIVATE link libraries from an OBJECT library.
#   aevox_core links asio::asio and llhttp::llhttp_static directly.
#   Do not add link_libraries here without updating src/CMakeLists.txt.

add_library(aevox_router OBJECT
    router_impl.cpp   # parse_pattern, trie insert/dispatch/group
    app_impl.cpp      # App lifecycle + connection handler
    middleware.cpp    # Middleware type-erased wrapper + operator()
)

target_include_directories(aevox_router
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src      # for #include "router/handler_wrap.hpp" etc.
        ${CMAKE_SOURCE_DIR}/include  # for #include <aevox/router.hpp> etc.
)

# app_impl.cpp includes net/websocket_session.hpp which uses Asio.
# Asio is header-only; linking PRIVATE gives the include path without propagating it.
target_link_libraries(aevox_router
    PRIVATE asio::asio
)

target_compile_definitions(aevox_router
    PRIVATE
        ASIO_STANDALONE
        ASIO_NO_DEPRECATED
)

target_compile_features(aevox_router
    PUBLIC cxx_std_23
)

target_compile_options(aevox_router
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)
