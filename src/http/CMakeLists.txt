# src/http/CMakeLists.txt
#
# aevox_http — OBJECT library containing the llhttp-backed HTTP/1.1 parser.
# Compiled into aevox_core via $<TARGET_OBJECTS:aevox_http>.
#
# llhttp types (llhttp_t, llhttp_settings_t) never escape this directory:
# http_parser.hpp uses a pimpl (HttpParser::Impl) to keep them out of any
# header that consumers might transitively include.
#
# IMPORTANT — OBJECT library transitive deps:
#   CMake does not propagate PRIVATE link libraries from an OBJECT library to
#   its consumer. aevox_core must therefore link llhttp::llhttp_static directly.
#   Do not change llhttp's link visibility here without updating src/CMakeLists.txt.

find_package(llhttp CONFIG REQUIRED)

add_library(aevox_http OBJECT
    http_parser.cpp      # AEV-003: HttpParser implementation (llhttp callbacks + Impl)
    request_impl.cpp     # AEV-005: Request non-template methods + explicit instantiations
    response_impl.cpp    # AEV-005: Response factory methods + builder methods
)

target_include_directories(aevox_http
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src    # for #include "http/http_parser.hpp" from within src/
        ${CMAKE_SOURCE_DIR}/include  # for aevox/* public headers (Task<T> etc.)
)

target_compile_features(aevox_http
    PUBLIC cxx_std_23
)

target_compile_options(aevox_http
    PRIVATE ${AEVOX_COMPILE_OPTIONS}
)

target_link_libraries(aevox_http
    PRIVATE llhttp::llhttp_static
)
