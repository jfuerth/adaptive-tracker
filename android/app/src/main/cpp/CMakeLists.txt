cmake_minimum_required(VERSION 3.22)
project(adaptive_tracker_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The portable core lives at the repository root. Host tools are off when it is
# not the top-level project.
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
add_subdirectory(${REPO_ROOT} adaptive_core)

find_package(oboe REQUIRED CONFIG)

add_library(adaptive_native SHARED audio_player.cpp jni_bridge.cpp)
target_link_libraries(adaptive_native PRIVATE adaptive_songfile oboe::oboe log)
target_compile_options(adaptive_native PRIVATE -Wall -Wextra)
