# src/plugins/CMakeLists.txt
# Optional first-party Aevox plugins.

if(AEVOX_ENABLE_GRPC)
    add_subdirectory(grpc)
endif()
