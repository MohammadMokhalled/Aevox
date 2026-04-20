# tests/integration/router/CMakeLists.txt
# Integration tests for aevox::App + aevox::Router over real loopback TCP.
# Uses real aevox::Executor + aevox::TcpStream — no mocking.

add_executable(router-e2e
    router-e2e.cpp
)

target_include_directories(router-e2e
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for #include "http/http_parser.hpp"
)

target_link_libraries(router-e2e
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(router-e2e
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(router-e2e)
add_dependencies(aevox_tests_integration router-e2e)
