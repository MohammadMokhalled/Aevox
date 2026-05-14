# tests/bench/CMakeLists.txt
#
# Performance benchmarks — throughput and latency baselines.
# Framework: nanobench (found by parent tests/CMakeLists.txt).
# Location convention: tests/bench/{module}/{slug}.cpp
#
# Required for tasks tagged #networking or #performance (PRD §9).

add_subdirectory(net)        # accept-loop throughput baseline
add_subdirectory(middleware) # middleware pipeline throughput benchmark
add_subdirectory(json)       # JSON serialization throughput baseline
add_subdirectory(log)        # logging hot-path latency and throughput benchmarks
add_subdirectory(websocket) # WebSocket throughput and frame-parse benchmarks
