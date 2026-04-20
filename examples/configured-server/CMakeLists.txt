# examples/configured-server/CMakeLists.txt
#
# Demonstrates App::create() with an optional TOML config file.
# Build: cmake --build build/debug --target configured-server

add_executable(configured-server main.cpp)

target_include_directories(configured-server PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(configured-server PRIVATE aevox_core)
target_compile_options(configured-server PRIVATE ${AEVOX_COMPILE_OPTIONS})
