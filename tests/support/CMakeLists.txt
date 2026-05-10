# tests/support/CMakeLists.txt
# Shared test support libraries. Backend dependencies stay here, not in test cases.

find_package(asio CONFIG REQUIRED)

add_library(aevox_websocket_test_support STATIC
    websocket_test_client.cpp
)

target_include_directories(aevox_websocket_test_support
    PUBLIC ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(aevox_websocket_test_support
    PRIVATE asio::asio
)

target_compile_definitions(aevox_websocket_test_support
    PRIVATE
        ASIO_STANDALONE
        ASIO_NO_DEPRECATED
)

target_compile_options(aevox_websocket_test_support
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)
