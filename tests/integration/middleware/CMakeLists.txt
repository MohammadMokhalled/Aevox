# tests/integration/middleware/CMakeLists.txt
# Integration tests for aevox::Middleware — real HTTP bytes over loopback TCP.
# Uses real aevox::App on an ephemeral port with Asio client sockets.
# No mocking, no synchronous hand-rolled coroutine drivers.

add_executable(middleware-e2e
    middleware-e2e.cpp
)

target_include_directories(middleware-e2e
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(middleware-e2e
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(middleware-e2e
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(middleware-e2e PROPERTIES RESOURCE_LOCK network)
add_dependencies(aevox_tests_integration middleware-e2e)
