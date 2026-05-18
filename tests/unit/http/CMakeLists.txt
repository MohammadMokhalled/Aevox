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

find_package(glaze CONFIG REQUIRED)

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
        glaze::glaze
        Catch2::Catch2WithMain
)

target_compile_definitions(request-response
    PRIVATE AEVOX_JSON_BACKEND_GLAZE
)

target_compile_options(request-response
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(request-response)
add_dependencies(aevox_tests_unit request-response)

# ---------------------------------------------------------------------------
# Traceparent parsing unit tests (AEV-012)
# ---------------------------------------------------------------------------

add_executable(traceparent-parse-valid
    traceparent-parse-valid.cpp
)

target_include_directories(traceparent-parse-valid
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for #include "http/traceparent.hpp"
)

target_link_libraries(traceparent-parse-valid
    PRIVATE
        Catch2::Catch2WithMain
)

target_compile_options(traceparent-parse-valid
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(traceparent-parse-valid)
add_dependencies(aevox_tests_unit traceparent-parse-valid)

add_executable(traceparent-parse-invalid
    traceparent-parse-invalid.cpp
)

target_include_directories(traceparent-parse-invalid
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for #include "http/traceparent.hpp"
)

target_link_libraries(traceparent-parse-invalid
    PRIVATE
        Catch2::Catch2WithMain
)

target_compile_options(traceparent-parse-invalid
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(traceparent-parse-invalid)
add_dependencies(aevox_tests_unit traceparent-parse-invalid)

# ---------------------------------------------------------------------------
# trace_context() unit tests (AEV-012)
# ---------------------------------------------------------------------------

add_executable(trace-context-return
    trace-context-return.cpp
)

target_include_directories(trace-context-return
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for #include "http/request_impl.hpp"
)

target_link_libraries(trace-context-return
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(trace-context-return
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(trace-context-return)
add_dependencies(aevox_tests_unit trace-context-return)
