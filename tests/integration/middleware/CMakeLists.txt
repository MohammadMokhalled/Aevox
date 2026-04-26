# tests/integration/middleware/CMakeLists.txt
# Integration tests for aevox::Middleware — real HTTP roundtrip with middleware.
# Uses real aevox::Executor + aevox::TestClient — no mocking.

add_executable(middleware-http-roundtrip
    http-roundtrip.cpp
)

target_include_directories(middleware-http-roundtrip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(middleware-http-roundtrip
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(middleware-http-roundtrip
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(middleware-http-roundtrip)
add_dependencies(aevox_tests_integration middleware-http-roundtrip)

# ---------------------------------------------------------------------------
# Auth Guard Middleware Integration Test
# ---------------------------------------------------------------------------

add_executable(middleware-auth-guard
    auth-guard.cpp
)

target_include_directories(middleware-auth-guard
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(middleware-auth-guard
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(middleware-auth-guard
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(middleware-auth-guard)
add_dependencies(aevox_tests_integration middleware-auth-guard)
