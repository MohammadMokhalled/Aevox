# tests/unit/http/CMakeLists.txt
# Unit tests for aevox::detail::HttpParser (internal — no I/O).
# Tests exercise http_parser.hpp directly via src/ include path.

add_executable(AEV-003-parser
    AEV-003-parser.cpp
)

target_include_directories(AEV-003-parser
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for #include "http/http_parser.hpp"
)

target_link_libraries(AEV-003-parser
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(AEV-003-parser
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(AEV-003-parser)
add_dependencies(aevox_tests_unit AEV-003-parser)

# ---------------------------------------------------------------------------
# AEV-005: Request / Response unit tests
# ---------------------------------------------------------------------------

add_executable(AEV-005-request-response
    AEV-005-request-response.cpp
)

target_include_directories(AEV-005-request-response
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for #include "http/request_impl.hpp" in test helper
)

target_link_libraries(AEV-005-request-response
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(AEV-005-request-response
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(AEV-005-request-response)
add_dependencies(aevox_tests_unit AEV-005-request-response)
