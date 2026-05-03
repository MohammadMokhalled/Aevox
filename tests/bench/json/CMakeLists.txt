# tests/bench/json/CMakeLists.txt
# JSON serialization throughput benchmarks.
# Establishes the glaze backend baseline for future regression tracking (AEV-015).

find_package(glaze CONFIG REQUIRED)

add_executable(json-serialization-throughput
    json-serialization-throughput.cpp
)

target_include_directories(json-serialization-throughput
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(json-serialization-throughput
    PRIVATE
        aevox_core
        glaze::glaze
        nanobench::nanobench
)

target_compile_definitions(json-serialization-throughput
    PRIVATE AEVOX_JSON_BACKEND_GLAZE
)

target_compile_options(json-serialization-throughput
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

add_test(NAME json-serialization-throughput COMMAND json-serialization-throughput)
set_tests_properties(json-serialization-throughput PROPERTIES LABELS bench)
add_dependencies(aevox_tests_bench json-serialization-throughput)
