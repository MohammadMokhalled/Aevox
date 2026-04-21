# tests/integration/CMakeLists.txt
#
# Integration tests — real I/O on loopback sockets.
# Framework: Catch2 3 + real asio::io_context (no Asio mocking permitted).
# Location convention: tests/integration/{module}/{slug}.cpp

add_subdirectory(net)    # TCP accept loop integration tests
add_subdirectory(http)   # HttpParser + TcpStream roundtrip integration tests
add_subdirectory(router) # App + Router end-to-end over loopback TCP
add_subdirectory(config) # Config file loading integration tests
