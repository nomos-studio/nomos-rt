# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.20)

project(nomos-rt
    VERSION 0.1.0
    DESCRIPTION "nomos-rt — shared C++ runtime substrate: Link, MIDI/OSC, IPC, txlog, modulators"
    HOMEPAGE_URL "https://github.com/nomos-studio/nomos-rt"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Dependency policy
#
# - No Boost. C++20 STL covers all use cases.
# - FetchContent + pinned tag for any dependency with a build step.
# - FETCHCONTENT_UPDATES_DISCONNECTED: skip network on subsequent invocations.
# - Tests use Catch2 v3 (Boost Software License, header-friendly).
# - SQLite3 via find_package — available on all target platforms.
# ---------------------------------------------------------------------------

include(FetchContent)
include(GNUInstallDirs)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
    "Skip network fetch if already populated")

# ---------------------------------------------------------------------------
# Build options
# ---------------------------------------------------------------------------

option(NOMOS_RT_BUILD_TESTS "Build nomos-rt unit tests" OFF)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# SQLite3 — system package on all target platforms (required by txlog).
find_package(SQLite3 REQUIRED)

# txlog — monorepo; the C++ library lives under cpp/.
#   To use a local checkout: cmake -DTXLOG_CPP_DIR=/path/to/txlog/cpp ...
if(DEFINED TXLOG_CPP_DIR)
    FetchContent_Declare(txlog-cpp SOURCE_DIR "${TXLOG_CPP_DIR}")
else()
    FetchContent_Declare(txlog-cpp
        GIT_REPOSITORY https://github.com/nomos-studio/txlog.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  cpp)
endif()
set(TXLOG_CPP_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(TXLOG_CPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(txlog-cpp)

# CLAP SDK — header-only C/C++ interface.
FetchContent_Declare(clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.6
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(clap)

# ---------------------------------------------------------------------------
# liburcu — vendored from third_party/liburcu/ (LGPL-2.1-or-later).
#
# Flavor selection:
#   macOS  → urcu-bp (bulletproof; no thread registration, no sys_membarrier)
#   Linux  → urcu-mb (memory barrier; sys_membarrier upgrade path deferred)
# ---------------------------------------------------------------------------

set(URCU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/liburcu")
set(URCU_GENERATED_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/urcu-include")

if(APPLE)
    set(URCU_CONFIG_SRC "${URCU_ROOT}/config/config-darwin.h")
else()
    set(URCU_CONFIG_SRC "${URCU_ROOT}/config/config-linux.h")
endif()
set(URCU_FLAVOR_SRC "${URCU_ROOT}/src/urcu-bp.c")

configure_file("${URCU_CONFIG_SRC}"
               "${URCU_GENERATED_INCLUDE}/urcu/config.h" COPYONLY)

add_library(urcu-flavor STATIC
    "${URCU_FLAVOR_SRC}"
    "${URCU_ROOT}/src/compat_futex.c"
    "${URCU_ROOT}/src/urcu-call-rcu-impl.h"
)

add_library(urcu::flavor ALIAS urcu-flavor)

target_include_directories(urcu-flavor
    PUBLIC
        "${URCU_ROOT}/include"
        "${URCU_GENERATED_INCLUDE}"
    PRIVATE
        "${URCU_ROOT}/src"
)

set_target_properties(urcu-flavor PROPERTIES
    C_STANDARD   11
    C_STANDARD_REQUIRED ON
)

# RtMidi — cross-platform MIDI I/O (MIT).
FetchContent_Declare(rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG        6.0.0
    GIT_SHALLOW    TRUE)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rtmidi)

# RtAudio — cross-platform audio I/O (MIT).
FetchContent_Declare(rtaudio
    GIT_REPOSITORY https://github.com/thestk/rtaudio.git
    GIT_TAG        6.0.1
    GIT_SHALLOW    TRUE)
set(RTAUDIO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTAUDIO_TARGETNAME_UNINSTALL rtaudio_uninstall CACHE STRING "" FORCE)
FetchContent_MakeAvailable(rtaudio)

# Ableton Link — beat-sync peer protocol (GPL-2.0-or-later).
#   To use a local checkout: cmake -DLINK_DIR=/path/to/link ...
if(DEFINED LINK_DIR)
    FetchContent_Declare(abl-link SOURCE_DIR "${LINK_DIR}")
else()
    FetchContent_Declare(abl-link
        GIT_REPOSITORY https://github.com/Ableton/link.git
        GIT_TAG        Link-3.1.1
        GIT_SHALLOW    TRUE)
endif()
FetchContent_GetProperties(abl-link)
if(NOT abl-link_POPULATED)
    FetchContent_Populate(abl-link)
endif()
include(${abl-link_SOURCE_DIR}/AbletonLinkConfig.cmake)

# ---------------------------------------------------------------------------
# nomos-rt — shared runtime substrate.
#
# Contains: IPC channel, session (txlog), rt_control_thread base, Link peer,
# MIDI I/O, OSC server, audio device, event infrastructure, modulators.
#
# Ableton::Link is GPL-2.0-or-later; nomos-rt inherits that license at link
# time. All consumers (kairos, aion) are therefore GPL at the binary level.
# ---------------------------------------------------------------------------

add_library(nomos-rt STATIC
    src/version.cpp
    src/ipc_channel.cpp
    src/session.cpp
    src/rt_control_thread.cpp
    src/link_peer.cpp
    src/midi_io.cpp
    src/event_collector.cpp
    src/input_event_buffer.cpp
    src/osc_server.cpp
    src/audio_device.cpp
    src/modulator_engine.cpp
    src/graph_modulator.cpp
    src/slope_modulator.cpp
    src/segment_modulator.cpp
    src/shift_register_modulator.cpp
    src/slew_modulator.cpp
    src/fractal_modulator.cpp
    src/stochastic_modulator.cpp
    src/cv_channel_decoder.cpp
)

add_library(nomos::rt ALIAS nomos-rt)

target_include_directories(nomos-rt
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        src
)

target_link_libraries(nomos-rt
    PUBLIC
        txlog::txlog
        clap
        urcu::flavor
        Ableton::Link
        rtmidi
        rtaudio
)

set_target_properties(nomos-rt PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    target_compile_options(nomos-rt PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    )
endif()

# Interface target that exposes the exe-level private headers (link_peer,
# midi_io, osc_server, audio_device, event_collector, etc.) for thin
# executables that link nomos-rt directly — primarily aion and kairos.
# These headers are GPL-2.0-or-later; consumers inherit that at binary link time.
add_library(nomos-rt-exe-headers INTERFACE)
add_library(nomos::rt-exe-headers ALIAS nomos-rt-exe-headers)
target_include_directories(nomos-rt-exe-headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(NOMOS_RT_BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_subdirectory(tests)
endif()
