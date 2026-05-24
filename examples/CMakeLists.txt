# examples/CMakeLists.txt
#
# Example applications demonstrating the Aevox public API.
# Built only when AEVOX_BUILD_EXAMPLES=ON.
# Location convention: examples/{example-name}/main.cpp

add_subdirectory(hello-world)
add_subdirectory(configured-server)
add_subdirectory(middleware-plugin)
add_subdirectory(static-files)
add_subdirectory(websocket-chat)
if(AEVOX_ENABLE_GRPC)
    add_subdirectory(grpc-echo)
endif()
