cmake_minimum_required(VERSION 3.27)

project(aevox_install_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(aevox REQUIRED)

add_executable(aevox_install_consumer main.cpp)
target_link_libraries(aevox_install_consumer PRIVATE aevox::aevox_core)
