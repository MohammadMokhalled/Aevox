# tests/bench/log/CMakeLists.txt
# Performance benchmarks for the AEV-029 logging subsystem.
# Framework: nanobench.

function(add_log_bench name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src   # for internal log headers
    )
    target_link_libraries(${name}
        PRIVATE
            aevox_core
            nanobench::nanobench
    )
    target_compile_options(${name} PRIVATE ${AEVOX_COMPILE_OPTIONS})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS bench)
    add_dependencies(aevox_tests_bench ${name})
endfunction()

add_log_bench(logger-hot-path)
