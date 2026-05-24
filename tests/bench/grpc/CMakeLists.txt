# tests/bench/grpc/CMakeLists.txt
# Unary gRPC baseline benchmark.

find_package(asio CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2)

add_executable(grpc-unary-throughput
    grpc-unary-throughput.cpp
)

target_include_directories(grpc-unary-throughput
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/plugins/grpc
        ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(grpc-unary-throughput
    PRIVATE
        aevox_grpc_plugin
        asio::asio
        PkgConfig::NGHTTP2
        nanobench::nanobench
)

target_compile_definitions(grpc-unary-throughput
    PRIVATE
        ASIO_STANDALONE
        ASIO_NO_DEPRECATED
)

target_compile_options(grpc-unary-throughput PRIVATE ${AEVOX_COMPILE_OPTIONS})

add_test(NAME grpc-unary-throughput COMMAND grpc-unary-throughput)
set_tests_properties(grpc-unary-throughput PROPERTIES LABELS bench)
add_dependencies(aevox_tests_bench grpc-unary-throughput)
