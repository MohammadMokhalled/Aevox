# tests/unit/json/CMakeLists.txt
# Unit tests for the JSON abstraction layer.
# Tests exercise GlazeBackend, JsonBackend concept, and JsonError directly.

find_package(glaze CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# json-error tests
# ---------------------------------------------------------------------------

add_executable(json-error-tests
    json-error.cpp
)

target_include_directories(json-error-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(json-error-tests
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(json-error-tests
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(json-error-tests)
add_dependencies(aevox_tests_unit json-error-tests)

# ---------------------------------------------------------------------------
# json-backend-concept tests
# ---------------------------------------------------------------------------

add_executable(json-backend-concept-tests
    json-backend-concept.cpp
)

target_include_directories(json-backend-concept-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(json-backend-concept-tests
    PRIVATE
        aevox_core
        glaze::glaze
        Catch2::Catch2WithMain
)

target_compile_definitions(json-backend-concept-tests
    PRIVATE AEVOX_JSON_BACKEND_GLAZE
)

target_compile_options(json-backend-concept-tests
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(json-backend-concept-tests)
add_dependencies(aevox_tests_unit json-backend-concept-tests)

# ---------------------------------------------------------------------------
# json-glaze-backend tests
# ---------------------------------------------------------------------------

add_executable(json-glaze-backend-tests
    json-glaze-backend.cpp
)

target_include_directories(json-glaze-backend-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(json-glaze-backend-tests
    PRIVATE
        aevox_core
        glaze::glaze
        Catch2::Catch2WithMain
)

target_compile_definitions(json-glaze-backend-tests
    PRIVATE AEVOX_JSON_BACKEND_GLAZE
)

target_compile_options(json-glaze-backend-tests
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(json-glaze-backend-tests)
add_dependencies(aevox_tests_unit json-glaze-backend-tests)
