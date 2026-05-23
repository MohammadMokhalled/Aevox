# examples/grpc-echo/CMakeLists.txt
# Optional raw-byte unary gRPC echo example.

add_executable(grpc-echo main.cpp)

target_include_directories(grpc-echo PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(grpc-echo PRIVATE aevox_core aevox_grpc_plugin)
target_compile_options(grpc-echo PRIVATE ${AEVOX_COMPILE_OPTIONS})
