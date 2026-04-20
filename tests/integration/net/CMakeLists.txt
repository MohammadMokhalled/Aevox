# tests/integration/net/CMakeLists.txt
# Integration tests for the TCP accept loop.
# Uses real asio::io_context on loopback — no mocking of Asio internals.

add_executable(tcp-accept
    tcp-accept.cpp
)

target_link_libraries(tcp-accept
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(tcp-accept
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(tcp-accept)
add_dependencies(aevox_tests_integration tcp-accept)

# Thread pool and async helpers integration tests
add_executable(thread-pool-integration
    thread-pool-integration.cpp
)

target_link_libraries(thread-pool-integration
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(thread-pool-integration
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(thread-pool-integration)
add_dependencies(aevox_tests_integration thread-pool-integration)
