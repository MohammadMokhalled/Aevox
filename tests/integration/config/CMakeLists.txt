# tests/integration/config/CMakeLists.txt
# Integration tests for the AEV-025 runtime configuration system.
# Tests write real TOML files to the filesystem and run a live App listen/stop cycle.

add_executable(config-file-roundtrip
    config-file-roundtrip.cpp
)

target_include_directories(config-file-roundtrip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for transitive router/handler_wrap.hpp via app.hpp
)

target_link_libraries(config-file-roundtrip
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(config-file-roundtrip
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(config-file-roundtrip)
add_dependencies(aevox_tests_integration config-file-roundtrip)
