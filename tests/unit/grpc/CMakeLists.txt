# tests/unit/grpc/CMakeLists.txt
# Unit tests for the optional official gRPC plugin.

add_executable(grpc-config-tests
    grpc-config.cpp
)

target_include_directories(grpc-config-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/plugins/grpc
)

target_link_libraries(grpc-config-tests
    PRIVATE
        aevox_grpc_plugin
        Catch2::Catch2WithMain
)

target_compile_options(grpc-config-tests PRIVATE ${AEVOX_COMPILE_OPTIONS})
catch_discover_tests(grpc-config-tests)
add_dependencies(aevox_tests_unit grpc-config-tests)

add_executable(grpc-envelope-tests
    grpc-envelope.cpp
)

target_include_directories(grpc-envelope-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/plugins/grpc
)

target_link_libraries(grpc-envelope-tests
    PRIVATE
        aevox_grpc_plugin
        Catch2::Catch2WithMain
)

target_compile_options(grpc-envelope-tests PRIVATE ${AEVOX_COMPILE_OPTIONS})
catch_discover_tests(grpc-envelope-tests)
add_dependencies(aevox_tests_unit grpc-envelope-tests)

add_executable(grpc-status-tests
    grpc-status.cpp
)

target_include_directories(grpc-status-tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(grpc-status-tests
    PRIVATE
        aevox_grpc_plugin
        Catch2::Catch2WithMain
)

target_compile_options(grpc-status-tests PRIVATE ${AEVOX_COMPILE_OPTIONS})
catch_discover_tests(grpc-status-tests)
add_dependencies(aevox_tests_unit grpc-status-tests)
