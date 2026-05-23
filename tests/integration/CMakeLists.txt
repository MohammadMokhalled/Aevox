# tests/integration/CMakeLists.txt
#
# Integration tests — real I/O on loopback sockets.
# Framework: Catch2 3 + real asio::io_context (no Asio mocking permitted).
# Location convention: tests/integration/{module}/{slug}.cpp

add_subdirectory(net)        # TCP accept loop integration tests
add_subdirectory(http)       # HttpParser + TcpStream roundtrip integration tests
add_subdirectory(router)     # App + Router end-to-end over loopback TCP
add_subdirectory(config)     # Config file loading integration tests
add_subdirectory(middleware) # Middleware pipeline integration tests with real HTTP
add_subdirectory(json)       # JSON request/response integration tests over loopback TCP
add_subdirectory(log)        # Logging middleware and multi-sink integration tests
add_subdirectory(websocket) # WebSocket upgrade, echo, close, and broadcast integration tests
if(AEVOX_ENABLE_GRPC)
    add_subdirectory(grpc)   # Optional official gRPC plugin loopback tests
endif()
