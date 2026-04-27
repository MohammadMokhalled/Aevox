# tests/unit/config/CMakeLists.txt
# Unit tests for the AEV-025 runtime configuration system.
# No I/O except writing temporary TOML files to disk.

add_executable(config-defaults
    config-defaults.cpp
)

target_include_directories(config-defaults
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for transitive router/handler_wrap.hpp via app.hpp
)

target_link_libraries(config-defaults
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(config-defaults
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(config-defaults)
add_dependencies(aevox_tests_unit config-defaults)
