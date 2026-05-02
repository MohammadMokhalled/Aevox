# tests/integration/json/CMakeLists.txt
# Integration tests for JSON request/response over real loopback TCP.
# Uses real aevox::App with Asio client sockets. No mocks.

find_package(glaze CONFIG REQUIRED)

add_executable(json-e2e
    json-request-response.cpp
)

target_include_directories(json-e2e
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(json-e2e
    PRIVATE
        aevox_core
        glaze::glaze
        Catch2::Catch2WithMain
)

target_compile_definitions(json-e2e
    PRIVATE AEVOX_JSON_BACKEND_GLAZE
)

target_compile_options(json-e2e
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(json-e2e PROPERTIES RESOURCE_LOCK network)
add_dependencies(aevox_tests_integration json-e2e)
