# tests/unit/net/CMakeLists.txt
# Unit tests for the aevox::Executor interface and aevox::Task<T>.
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
