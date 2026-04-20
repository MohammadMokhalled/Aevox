# tests/unit/http/CMakeLists.txt
# Unit tests for aevox::detail::HttpParser (internal — no I/O).
# Tests exercise http_parser.hpp directly via src/ include path.

add_executable(http-parser
    http-parser.cpp
)

target_include_directories(http-parser
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for #include "http/http_parser.hpp"
)

target_link_libraries(http-parser
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(http-parser
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(http-parser)
add_dependencies(aevox_tests_unit http-parser)

# ---------------------------------------------------------------------------
# Request / Response unit tests
# ---------------------------------------------------------------------------

add_executable(request-response
    request-response.cpp
)

target_include_directories(request-response
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for #include "http/request_impl.hpp" in test helper
)

target_link_libraries(request-response
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(request-response
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(request-response)
add_dependencies(aevox_tests_unit request-response)
