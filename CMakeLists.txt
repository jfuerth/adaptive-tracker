cmake_minimum_required(VERSION 3.22)
project(adaptive_tracker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PRESSENSE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/pressense)
if(NOT EXISTS ${PRESSENSE_DIR}/lib/synth/voice.hpp)
    message(FATAL_ERROR "pressense submodule missing: run `git submodule update --init core/third_party/pressense`")
endif()

# Portable engine core: header-only, no heap after setup, no exceptions/RTTI needed.
add_library(adaptive_engine INTERFACE)
target_include_directories(adaptive_engine INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/core/engine/include)
target_include_directories(adaptive_engine SYSTEM INTERFACE
    ${PRESSENSE_DIR}/lib/synth
    ${PRESSENSE_DIR}/lib/features
    ${PRESSENSE_DIR}/lib/nlohmann)

# Demo songs shared by desktop tools and the Android app.
add_library(adaptive_demo INTERFACE)
target_include_directories(adaptive_demo INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(adaptive_demo INTERFACE adaptive_engine)

# Song files (JSON): phone and desktop only, not microcontrollers.
add_library(adaptive_songfile STATIC core/songfile/songfile.cpp)
target_include_directories(adaptive_songfile PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/songfile/include)
target_link_libraries(adaptive_songfile PUBLIC adaptive_engine)

set(ADAPTIVE_WARNINGS -Wall -Wextra -Wpedantic)
target_compile_options(adaptive_songfile PRIVATE ${ADAPTIVE_WARNINGS})

# Host-only targets: tests and desktop tools.
option(ADAPTIVE_BUILD_HOST_TOOLS "Build unit tests and desktop tools" ${PROJECT_IS_TOP_LEVEL})
if(ADAPTIVE_BUILD_HOST_TOOLS)
    enable_testing()

    add_executable(adaptive_tests
        core/tests/test_main.cpp
        core/tests/test_engine.cpp
        core/tests/test_pitch.cpp
        core/tests/test_sequencer.cpp
        core/tests/test_adaptive.cpp
        core/tests/test_mappings.cpp
        core/tests/test_demo.cpp
        core/tests/test_editor_engine.cpp
        core/tests/test_inspect.cpp
        core/tests/test_songfile.cpp
        core/tests/test_pattern_edits.cpp
        core/tests/test_song_edits.cpp
        core/tests/test_columns.cpp
        core/tests/test_song_swap.cpp
        core/tests/test_mixer.cpp)
    find_package(Threads REQUIRED)
    target_link_libraries(adaptive_tests PRIVATE Threads::Threads)
    target_link_libraries(adaptive_tests PRIVATE adaptive_demo adaptive_songfile)
    target_compile_definitions(adaptive_tests PRIVATE ADAPTIVE_SONGS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/songs")
    target_include_directories(adaptive_tests SYSTEM PRIVATE core/third_party/doctest)
    target_compile_options(adaptive_tests PRIVATE ${ADAPTIVE_WARNINGS})
    add_test(NAME adaptive_tests COMMAND adaptive_tests)

    # Fails the build if the engine stops compiling under MCU-style flags.
    add_library(adaptive_portability_check OBJECT core/tests/portability_check.cpp)
    target_link_libraries(adaptive_portability_check PRIVATE adaptive_engine)
    target_compile_options(adaptive_portability_check PRIVATE
        ${ADAPTIVE_WARNINGS} -Werror -fno-exceptions -fno-rtti)

    add_executable(render_demo desktop/render_demo.cpp)
    target_link_libraries(render_demo PRIVATE adaptive_engine)
    target_compile_options(render_demo PRIVATE ${ADAPTIVE_WARNINGS})

    add_executable(adaptive_render desktop/adaptive_render.cpp)
    target_link_libraries(adaptive_render PRIVATE adaptive_demo)
    target_compile_options(adaptive_render PRIVATE ${ADAPTIVE_WARNINGS})

    # Writes the demo song as a song file (songs/adaptive-demo.json).
    add_executable(export_demo desktop/export_demo.cpp)
    target_link_libraries(export_demo PRIVATE adaptive_demo adaptive_songfile)
    target_compile_options(export_demo PRIVATE ${ADAPTIVE_WARNINGS})

    add_executable(key_experiment desktop/key_experiment.cpp)
    target_link_libraries(key_experiment PRIVATE adaptive_demo)
    target_compile_options(key_experiment PRIVATE ${ADAPTIVE_WARNINGS})
endif()
