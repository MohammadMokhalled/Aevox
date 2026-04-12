# tests/unit/CMakeLists.txt
#
# Unit tests — fast, isolated, no I/O.
# Framework: Catch2 3 (found by parent tests/CMakeLists.txt).
# Location convention: tests/unit/{module}/{TASK-ID}-{slug}.cpp

add_subdirectory(net)   # AEV-001: Executor interface + Task<T> unit tests
