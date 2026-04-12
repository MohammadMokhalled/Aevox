# tests/bench/CMakeLists.txt
#
# Performance benchmarks — throughput and latency baselines.
# Framework: nanobench (found by parent tests/CMakeLists.txt).
# Location convention: tests/bench/{module}/{TASK-ID}-{slug}.cpp
#
# Required for tasks tagged #networking or #performance (PRD §9).

add_subdirectory(net)   # AEV-001: accept-loop throughput baseline
