# tests/integration/http/CMakeLists.txt
# Integration tests for HttpParser over real TCP loopback.
# Uses real aevox::Executor + aevox::TcpStream — no mocking.

add_executable(http-parse-roundtrip
    http-parse-roundtrip.cpp
)

target_include_directories(http-parse-roundtrip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for #include "http/http_parser.hpp"
)

target_link_libraries(http-parse-roundtrip
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(http-parse-roundtrip
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(http-parse-roundtrip)
add_dependencies(aevox_tests_integration http-parse-roundtrip)
