# tests/unit/log/CMakeLists.txt
# Unit tests for the AEV-029 logging subsystem.
# Framework: Catch2 3.

function(add_log_test name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src   # for internal log headers
    )
    target_link_libraries(${name}
        PRIVATE
            aevox_core
            Catch2::Catch2WithMain
    )
    target_compile_options(${name} PRIVATE ${AEVOX_COMPILE_OPTIONS})
    catch_discover_tests(${name})
    add_dependencies(aevox_tests_unit ${name})
endfunction()

add_log_test(logger-format)
add_log_test(logger-filtering)
add_log_test(logger-request-context)
