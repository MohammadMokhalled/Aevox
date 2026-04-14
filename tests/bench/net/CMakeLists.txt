# tests/bench/net/CMakeLists.txt
# Throughput baseline for the TCP accept loop.
# Target: >= 500,000 accepted connections/sec on loopback (PRD §9).

add_executable(AEV-001-accept-throughput
    AEV-001-accept-throughput.cpp
)

target_link_libraries(AEV-001-accept-throughput
    PRIVATE
        aevox_core
        nanobench::nanobench
)

target_compile_options(AEV-001-accept-throughput
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

add_test(NAME AEV-001-accept-throughput COMMAND AEV-001-accept-throughput)
add_dependencies(aevox_tests_bench AEV-001-accept-throughput)

# AEV-006: Task dispatch and pool() latency benchmarks
add_executable(AEV-006-executor-throughput
    AEV-006-executor-throughput.cpp
)

target_link_libraries(AEV-006-executor-throughput
    PRIVATE
        aevox_core
        nanobench::nanobench
)

target_compile_options(AEV-006-executor-throughput
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

add_test(NAME AEV-006-executor-throughput COMMAND AEV-006-executor-throughput)
add_dependencies(aevox_tests_bench AEV-006-executor-throughput)
