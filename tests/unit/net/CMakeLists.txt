# tests/unit/net/CMakeLists.txt
# Unit tests for the aevox::Executor interface, aevox::Task<T>, and async helpers.
# All tests go through the public aevox:: interface — never AsioExecutor directly.

add_executable(executor-contract
    executor-contract.cpp
)

target_link_libraries(executor-contract
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(executor-contract
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(executor-contract)
add_dependencies(aevox_tests_unit executor-contract)

# pool(), sleep(), when_all() unit tests
add_executable(executor-async
    executor-async.cpp
)

target_link_libraries(executor-async
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(executor-async
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(executor-async)
add_dependencies(aevox_tests_unit executor-async)
