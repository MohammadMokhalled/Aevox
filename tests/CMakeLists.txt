# tests/CMakeLists.txt
#
# Test infrastructure for Aevox. Four categories:
#   smoke/        — build-system validation (CTest only, no Catch2 needed)
#   unit/         — fast, isolated, no I/O (Catch2)
#   integration/  — real I/O on loopback sockets (Catch2 + real Asio, no mocks)
#   bench/        — throughput baselines (nanobench)
#
# Requires: enable_testing() called in root CMakeLists.txt (it is).
# Provides: aevox_tests_unit, aevox_tests_integration, aevox_tests_bench targets.

find_package(Catch2 3 CONFIG REQUIRED)
find_package(nanobench CONFIG REQUIRED)

include(CTest)
include(Catch)

# ── Alias build targets ───────────────────────────────────────────────────────
# `cmake --build --preset default --target aevox_tests_unit` builds all unit tests.
add_custom_target(aevox_tests_unit)
add_custom_target(aevox_tests_integration)
add_custom_target(aevox_tests_bench)

# ── Subdirectories ────────────────────────────────────────────────────────────
add_subdirectory(smoke)
add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(bench)
