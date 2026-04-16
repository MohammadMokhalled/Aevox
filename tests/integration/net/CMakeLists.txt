# tests/integration/net/CMakeLists.txt
# Integration tests for the TCP accept loop.
# Uses real asio::io_context on loopback — no mocking of Asio internals.

add_executable(AEV-001-tcp-accept
    AEV-001-tcp-accept.cpp
)

target_link_libraries(AEV-001-tcp-accept
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(AEV-001-tcp-accept
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(AEV-001-tcp-accept)
add_dependencies(aevox_tests_integration AEV-001-tcp-accept)

# AEV-006: thread-pool and async helpers integration tests
add_executable(AEV-006-thread-pool-integration
    AEV-006-thread-pool-integration.cpp
)

target_link_libraries(AEV-006-thread-pool-integration
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(AEV-006-thread-pool-integration
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(AEV-006-thread-pool-integration)
add_dependencies(aevox_tests_integration AEV-006-thread-pool-integration)
