# tests/integration/websocket/CMakeLists.txt
# Integration tests for WebSocket upgrade, echo, close, and broadcast.
# All tests use real aevox::App + real Asio TCP sockets. No mocks.

find_package(asio CONFIG REQUIRED)

# WebSocket upgrade integration tests.
add_executable(websocket-upgrade-integration
    websocket-upgrade.cpp
)

target_include_directories(websocket-upgrade-integration
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(websocket-upgrade-integration
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
        asio::asio
)

target_compile_definitions(websocket-upgrade-integration
    PRIVATE
        ASIO_STANDALONE
        ASIO_NO_DEPRECATED
)

target_compile_options(websocket-upgrade-integration
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(websocket-upgrade-integration)
add_dependencies(aevox_tests_integration websocket-upgrade-integration)

# WebSocket echo and close integration tests.
add_executable(websocket-echo-integration
    websocket-echo.cpp
)

target_include_directories(websocket-echo-integration
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(websocket-echo-integration
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
        asio::asio
)

target_compile_definitions(websocket-echo-integration
    PRIVATE
        ASIO_STANDALONE
        ASIO_NO_DEPRECATED
)

target_compile_options(websocket-echo-integration
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(websocket-echo-integration)
add_dependencies(aevox_tests_integration websocket-echo-integration)

# WebSocket broadcast integration tests.
add_executable(websocket-broadcast-integration
    websocket-broadcast.cpp
)

target_include_directories(websocket-broadcast-integration
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(websocket-broadcast-integration
    PRIVATE
        aevox_core
        Catch2::Catch2WithMain
        asio::asio
)

target_compile_definitions(websocket-broadcast-integration
    PRIVATE
        ASIO_STANDALONE
        ASIO_NO_DEPRECATED
)

target_compile_options(websocket-broadcast-integration
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(websocket-broadcast-integration)
add_dependencies(aevox_tests_integration websocket-broadcast-integration)
