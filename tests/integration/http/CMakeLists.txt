# tests/integration/http/CMakeLists.txt
# Integration tests for HttpParser over real TCP loopback.
# Uses real aevox::Executor + aevox::TcpStream — no mocking.

add_executable(AEV-003-http-parse-roundtrip
    AEV-003-http-parse-roundtrip.cpp
)

target_include_directories(AEV-003-http-parse-roundtrip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for #include "http/http_parser.hpp"
)

target_link_libraries(AEV-003-http-parse-roundtrip
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(AEV-003-http-parse-roundtrip
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(AEV-003-http-parse-roundtrip)
add_dependencies(aevox_tests_integration AEV-003-http-parse-roundtrip)
