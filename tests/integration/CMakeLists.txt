# tests/integration/CMakeLists.txt
#
# Integration tests — real I/O on loopback sockets.
# Framework: Catch2 3 + real asio::io_context (no Asio mocking permitted).
# Location convention: tests/integration/{module}/{TASK-ID}-{slug}.cpp

add_subdirectory(net)   # AEV-001: TCP accept loop integration tests
