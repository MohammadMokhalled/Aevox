# tests/unit/net/CMakeLists.txt
# Unit tests for the aevox::Executor interface, aevox::Task<T>, and async helpers.
# All tests go through the public aevox:: interface — never AsioExecutor directly.

add_executable(AEV-001-executor-contract
    AEV-001-executor-contract.cpp
)

target_link_libraries(AEV-001-executor-contract
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(AEV-001-executor-contract
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(AEV-001-executor-contract)
add_dependencies(aevox_tests_unit AEV-001-executor-contract)

# AEV-006: pool(), sleep(), when_all() unit tests
add_executable(AEV-006-executor
    AEV-006-executor.cpp
)

target_link_libraries(AEV-006-executor
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(AEV-006-executor
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(AEV-006-executor)
add_dependencies(aevox_tests_unit AEV-006-executor)
