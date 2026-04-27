# examples/middleware-plugin/CMakeLists.txt
#
# Demonstrates middleware authoring and composition with the Aevox public API.
# Built as part of the default build — ensures examples always compile against the current API.
# Build: cmake --build --preset default --target middleware-plugin

add_executable(middleware-plugin main.cpp)

# router_impl.hpp (included transitively via app.hpp) requires src/ on the path
# to resolve router/handler_wrap.hpp. This mirrors the hello-world convention.
target_include_directories(middleware-plugin PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(middleware-plugin PRIVATE aevox_core)
target_compile_options(middleware-plugin PRIVATE ${AEVOX_COMPILE_OPTIONS})
