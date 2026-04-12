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
