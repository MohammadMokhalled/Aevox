# tests/bench/websocket/CMakeLists.txt
# WebSocket throughput, frame-parse, and topic-bus fanout benchmarks.
# Establishes baselines for AEV-015 regression gate.

add_executable(websocket-throughput
    websocket-throughput.cpp
)

target_include_directories(websocket-throughput
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for #include "net/websocket_frame.hpp" etc.
)

target_link_libraries(websocket-throughput
    PRIVATE
        aevox_core
        nanobench::nanobench
)

target_compile_options(websocket-throughput
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

add_test(NAME websocket-throughput COMMAND websocket-throughput)
set_tests_properties(websocket-throughput PROPERTIES LABELS bench)
add_dependencies(aevox_tests_bench websocket-throughput)
