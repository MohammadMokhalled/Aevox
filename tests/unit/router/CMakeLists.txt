# tests/unit/router/CMakeLists.txt
# Unit tests for aevox::Router path matching and dispatch.
# No I/O — exercises Router::dispatch synchronously via drive_task<T>.

add_executable(router
    router.cpp
)

target_include_directories(router
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for #include "http/request_impl.hpp" etc.
)

target_link_libraries(router
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(router
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(router)
add_dependencies(aevox_tests_unit router)
