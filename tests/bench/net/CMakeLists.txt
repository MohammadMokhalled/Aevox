# tests/bench/net/CMakeLists.txt
# Throughput baseline for the TCP accept loop.
# Target: >= 500,000 accepted connections/sec on loopback (PRD §9).

add_executable(accept-throughput
    accept-throughput.cpp
)

target_link_libraries(accept-throughput
    PRIVATE
        aevox_core
        nanobench::nanobench
)

target_compile_options(accept-throughput
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

add_test(NAME accept-throughput COMMAND accept-throughput)
add_dependencies(aevox_tests_bench accept-throughput)

# Task dispatch and pool() latency benchmarks
add_executable(executor-throughput
    executor-throughput.cpp
)

target_link_libraries(executor-throughput
    PRIVATE
        aevox_core
        nanobench::nanobench
)

target_compile_options(executor-throughput
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

add_test(NAME executor-throughput COMMAND executor-throughput)
add_dependencies(aevox_tests_bench executor-throughput)
