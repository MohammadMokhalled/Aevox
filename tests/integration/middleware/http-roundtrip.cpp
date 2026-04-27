// MOVED: This file has been reclassified as a unit test.
// See tests/unit/middleware/middleware-http-roundtrip.cpp
//
// Reason: Uses drive_task() (synchronous, no Asio io_context, no network I/O).
// Per CLAUDE.md §9: integration tests must use a real asio::io_context with
// loopback sockets. Real integration tests are in middleware-e2e.cpp.
