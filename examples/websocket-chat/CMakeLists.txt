# examples/websocket-chat/CMakeLists.txt
# Minimal WebSocket chat example demonstrating room-based broadcast.

add_executable(websocket-chat
    main.cpp
)

# router_impl.hpp (included via app.hpp) requires src/ on the path to resolve
# router/handler_wrap.hpp. All in-tree consumers must add this include directory.
target_include_directories(websocket-chat PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(websocket-chat
    PRIVATE aevox_core
)

target_compile_options(websocket-chat
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)
