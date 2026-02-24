cmake_minimum_required(VERSION 3.10)
project(MoleHole)

include(scripts/compiler_flags.cmake)
include(scripts/physx.cmake)

file(GLOB SOURCES
        src/*.cpp
        src/Core/*.cpp
        src/Application/*.cpp
        src/Simulation/*.cpp
        src/Renderer/*.cpp
        src/Renderer/Interface/*.cpp
        src/Renderer/Models/*.cpp
        src/Renderer/Modules/*.cpp
        src/UI/*.cpp)

if(WIN32)
    file(GLOB PLATFORM_SOURCES
            src/Platform/Windows/*.cpp
            src/Platform/GLFW/*.cpp
            src/Platform/OpenGL/*.cpp
            src/Platform/Vulkan/*.cpp)
elseif(UNIX)
    file(GLOB PLATFORM_SOURCES
            src/Platform/Linux/*.cpp
            src/Platform/GLFW/*.cpp
            src/Platform/OpenGL/*.cpp
            src/Platform/Vulkan/*.cpp)
endif()

list(APPEND SOURCES ${PLATFORM_SOURCES})

add_executable(MoleHole ${SOURCES})

target_precompile_headers(MoleHole PRIVATE src/mhpch.h)

set_property(TARGET MoleHole PROPERTY CXX_STANDARD 23)
set_property(TARGET MoleHole PROPERTY CXX_STANDARD_REQUIRED ON)

target_compile_definitions(MoleHole PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:RelWithDebInfo>:NDEBUG>
    $<$<CONFIG:MinSizeRel>:NDEBUG>
)

# ── Dependencies ──────────────────────────────────────────────────────────────

include(scripts/vulkan.cmake)
include(scripts/dependencies.cmake)
include(scripts/platform.cmake)
include(scripts/ffmpeg.cmake)

# ── Tests ─────────────────────────────────────────────────────────────────────

option(MOLEHOLE_BUILD_TESTS "Build unit tests and benchmarks" OFF)
if(MOLEHOLE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
