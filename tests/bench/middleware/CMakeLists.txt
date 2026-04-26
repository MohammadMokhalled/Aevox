# tests/bench/middleware/CMakeLists.txt
# Performance benchmark: middleware pipeline throughput.
# Measures overhead of middleware composition vs. direct dispatch.
# Target: < 5% overhead with 10 chained middlewares (PRD §14).

add_executable(pipeline-throughput
    pipeline-throughput.cpp
)

target_include_directories(pipeline-throughput
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(pipeline-throughput
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(pipeline-throughput
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

add_test(NAME pipeline-throughput COMMAND pipeline-throughput)
add_dependencies(aevox_tests_bench pipeline-throughput)
