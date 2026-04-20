# tests/unit/CMakeLists.txt
#
# Unit tests — fast, isolated, no I/O.
# Framework: Catch2 3 (found by parent tests/CMakeLists.txt).
# Location convention: tests/unit/{module}/{slug}.cpp

add_subdirectory(net)    # Executor interface + Task<T> unit tests
add_subdirectory(http)   # HttpParser unit tests
add_subdirectory(router) # Router path matching + dispatch unit tests
