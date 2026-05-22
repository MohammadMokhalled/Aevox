# tests/unit/bench/CMakeLists.txt
# Unit tests for benchmark support helpers.

add_executable(bench-latency-stats
    latency-stats.cpp
)

target_include_directories(bench-latency-stats
    PRIVATE
        ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(bench-latency-stats
    PRIVATE
        Catch2::Catch2WithMain
)

target_compile_options(bench-latency-stats
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(bench-latency-stats)
add_dependencies(aevox_tests_unit bench-latency-stats)
