# tests/unit/websocket/CMakeLists.txt
# Unit tests for WebSocket handshake, frame parsing, error type, and topic bus.
# All tests are isolated — no I/O, no Asio runtime required.

# WebSocket handshake unit tests (accept-key computation, validation).
add_executable(websocket-handshake
    websocket-handshake.cpp
)

target_include_directories(websocket-handshake
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src   # for #include "net/websocket_handshake.hpp"
)

target_link_libraries(websocket-handshake
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(websocket-handshake
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(websocket-handshake)
add_dependencies(aevox_tests_unit websocket-handshake)

# WebSocket frame parser/emitter unit tests.
add_executable(websocket-frame
    websocket-frame.cpp
)

target_include_directories(websocket-frame
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(websocket-frame
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(websocket-frame
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(websocket-frame)
add_dependencies(aevox_tests_unit websocket-frame)

# WebSocketError type unit tests.
add_executable(websocket-error
    websocket-error.cpp
)

target_link_libraries(websocket-error
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(websocket-error
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(websocket-error)
add_dependencies(aevox_tests_unit websocket-error)

# TopicBus pub/sub registry unit tests (includes thread-safety test).
add_executable(topic-bus
    topic-bus.cpp
)

target_include_directories(topic-bus
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(topic-bus
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
)

target_compile_options(topic-bus
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(topic-bus)
add_dependencies(aevox_tests_unit topic-bus)
