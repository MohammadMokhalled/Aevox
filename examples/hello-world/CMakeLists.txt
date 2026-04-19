# examples/hello-world/CMakeLists.txt
#
# Demonstrates the complete Aevox v0.1 public API in a single file.
# Not built by default (parent sets EXCLUDE_FROM_ALL).
# Build: cmake --build --preset default --target hello-world

add_executable(hello-world main.cpp)

# router_impl.hpp (included transitively via app.hpp) requires src/ on the path
# to resolve router/handler_wrap.hpp. All in-tree consumers add this; external
# consumers must too until the template header design is revised (tracked issue).
target_include_directories(hello-world PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(hello-world PRIVATE aevox_core)
target_compile_options(hello-world PRIVATE ${AEVOX_COMPILE_OPTIONS})
