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
