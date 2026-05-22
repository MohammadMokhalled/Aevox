# tests/bench/http/CMakeLists.txt
# HTTP keep-alive request/response throughput benchmark.

find_package(asio CONFIG REQUIRED)

add_executable(http-keepalive-throughput
    http-keepalive-throughput.cpp
)

target_include_directories(http-keepalive-throughput
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(http-keepalive-throughput
    PRIVATE
        aevox_core
        asio::asio
        nanobench::nanobench
)

target_compile_definitions(http-keepalive-throughput
    PRIVATE
        ASIO_STANDALONE
        ASIO_NO_DEPRECATED
)

target_compile_options(http-keepalive-throughput
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

add_test(NAME http-keepalive-throughput COMMAND http-keepalive-throughput)
set_tests_properties(http-keepalive-throughput PROPERTIES LABELS bench)
add_dependencies(aevox_tests_bench http-keepalive-throughput)
