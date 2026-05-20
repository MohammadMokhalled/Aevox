# examples/static-files/CMakeLists.txt
#
# Demonstrates aevox::middleware::static_files() with an application-owned
# public directory bundled beside the example.
# Build: cmake --build --preset default --target static-files-example

add_executable(static-files-example main.cpp)

target_include_directories(static-files-example PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(static-files-example PRIVATE aevox_core)
target_compile_options(static-files-example PRIVATE ${AEVOX_COMPILE_OPTIONS})
target_compile_definitions(static-files-example
    PRIVATE
        AEVOX_STATIC_FILES_EXAMPLE_PUBLIC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/public"
)
