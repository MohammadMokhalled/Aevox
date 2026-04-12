# tests/smoke/CMakeLists.txt
#
# Build-system smoke tests. These verify the project skeleton is correct:
# correct include paths, linker graph, and C++23 feature availability.
# They contain no business logic — compilation + exit-0 is the test.

add_executable(AEV-002-smoke
    AEV-002-compile-smoke.cpp
)

target_link_libraries(AEV-002-smoke
    PRIVATE aevox_core
)

target_compile_options(AEV-002-smoke
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

add_test(NAME AEV-002-smoke COMMAND AEV-002-smoke)

# Register under the unit alias so `--target aevox_tests_unit` includes it.
add_dependencies(aevox_tests_unit AEV-002-smoke)
