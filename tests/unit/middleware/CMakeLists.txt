# tests/unit/middleware/CMakeLists.txt
# Unit tests for aevox::Middleware — composable async middleware pipeline.

add_executable(middleware-ordering
    middleware-ordering.cpp
)

target_include_directories(middleware-ordering
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(middleware-ordering
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(middleware-ordering
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(middleware-ordering)
add_dependencies(aevox_tests_unit middleware-ordering)

# ---------------------------------------------------------------------------
# HTTP roundtrip unit test (reclassified from integration/ — uses drive_task,
# no Asio io_context, no network I/O)
# ---------------------------------------------------------------------------

add_executable(middleware-http-roundtrip
    middleware-http-roundtrip.cpp
)

target_include_directories(middleware-http-roundtrip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(middleware-http-roundtrip
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(middleware-http-roundtrip
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(middleware-http-roundtrip)
add_dependencies(aevox_tests_unit middleware-http-roundtrip)

# ---------------------------------------------------------------------------
# Auth guard unit test (reclassified from integration/ — uses drive_task,
# no Asio io_context, no network I/O)
# ---------------------------------------------------------------------------

add_executable(middleware-auth-guard
    middleware-auth-guard.cpp
)

target_include_directories(middleware-auth-guard
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(middleware-auth-guard
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(middleware-auth-guard
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(middleware-auth-guard)
add_dependencies(aevox_tests_unit middleware-auth-guard)

# ---------------------------------------------------------------------------
# Static file middleware
# ---------------------------------------------------------------------------

add_executable(static-files
    static-files.cpp
)

target_include_directories(static-files
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(static-files
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(static-files
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(static-files)
add_dependencies(aevox_tests_unit static-files)
