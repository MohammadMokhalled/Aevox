# tests/integration/grpc/CMakeLists.txt
# Integration tests for the optional official gRPC plugin.

find_package(asio CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2)

add_executable(grpc-unary-loopback-integration
    grpc-unary-loopback.cpp
)

target_include_directories(grpc-unary-loopback-integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(grpc-unary-loopback-integration
    PRIVATE
        aevox_grpc_plugin
        asio::asio
        PkgConfig::NGHTTP2
        Catch2::Catch2WithMain
)

target_compile_definitions(grpc-unary-loopback-integration
    PRIVATE
        ASIO_STANDALONE
        ASIO_NO_DEPRECATED
)

target_compile_options(grpc-unary-loopback-integration PRIVATE ${AEVOX_COMPILE_OPTIONS})
catch_discover_tests(grpc-unary-loopback-integration)
add_dependencies(aevox_tests_integration grpc-unary-loopback-integration)

add_executable(grpc-app-lifecycle-integration
    grpc-app-lifecycle.cpp
)

target_include_directories(grpc-app-lifecycle-integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(grpc-app-lifecycle-integration
    PRIVATE
        aevox_grpc_plugin
        asio::asio
        PkgConfig::NGHTTP2
        Catch2::Catch2WithMain
)

target_compile_definitions(grpc-app-lifecycle-integration
    PRIVATE
        ASIO_STANDALONE
        ASIO_NO_DEPRECATED
)

target_compile_options(grpc-app-lifecycle-integration PRIVATE ${AEVOX_COMPILE_OPTIONS})
catch_discover_tests(grpc-app-lifecycle-integration)
add_dependencies(aevox_tests_integration grpc-app-lifecycle-integration)
