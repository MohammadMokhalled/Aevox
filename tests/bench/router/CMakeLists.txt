# tests/bench/router/CMakeLists.txt
# Router dispatch throughput and latency benchmark.

add_executable(router-dispatch-throughput
    router-dispatch-throughput.cpp
)

target_include_directories(router-dispatch-throughput
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(router-dispatch-throughput
    PRIVATE
        aevox_core
        nanobench::nanobench
)

target_compile_options(router-dispatch-throughput
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

add_test(NAME router-dispatch-throughput COMMAND router-dispatch-throughput)
set_tests_properties(router-dispatch-throughput PROPERTIES LABELS bench)
add_dependencies(aevox_tests_bench router-dispatch-throughput)
