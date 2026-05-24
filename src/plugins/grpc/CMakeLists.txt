# src/plugins/grpc/CMakeLists.txt
# Optional official Aevox gRPC plugin.

find_package(PkgConfig REQUIRED)
pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2)

add_library(aevox_grpc_plugin STATIC
    grpc_plugin.cpp
    grpc_error.cpp
    grpc_envelope.cpp
    grpc_http2_session.cpp
)

target_include_directories(aevox_grpc_plugin
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(aevox_grpc_plugin
    PUBLIC
        aevox_core
    PRIVATE
        PkgConfig::NGHTTP2
)

target_compile_features(aevox_grpc_plugin PUBLIC cxx_std_23)
target_compile_options(aevox_grpc_plugin PRIVATE ${AEVOX_COMPILE_OPTIONS})
