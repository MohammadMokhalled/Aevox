# tests/integration/websocket/CMakeLists.txt
# Integration tests for WebSocket upgrade, echo, close, and broadcast.
# All tests use real aevox::App + Aevox-owned test support transport. No mocks.

# WebSocket upgrade integration tests.
add_executable(websocket-upgrade-integration
    websocket-upgrade.cpp
)

target_include_directories(websocket-upgrade-integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(websocket-upgrade-integration
    PRIVATE
        aevox_core
        aevox_websocket_test_support
        Catch2::Catch2WithMain
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
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(websocket-echo-integration
    PRIVATE
        aevox_core
        aevox_websocket_test_support
        Catch2::Catch2WithMain
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
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(websocket-broadcast-integration
    PRIVATE
        aevox_core
        aevox_websocket_test_support
        Catch2::Catch2WithMain
)

target_compile_options(websocket-broadcast-integration
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

catch_discover_tests(websocket-broadcast-integration)
add_dependencies(aevox_tests_integration websocket-broadcast-integration)
