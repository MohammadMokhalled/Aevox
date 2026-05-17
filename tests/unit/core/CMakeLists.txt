# tests/unit/core/CMakeLists.txt
# Unit tests for shared public core API helpers.

add_executable(error-model-tests
    error-model.cpp
)

target_include_directories(error-model-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(error-model-tests
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(error-model-tests
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(error-model-tests)
add_dependencies(aevox_tests_unit error-model-tests)
