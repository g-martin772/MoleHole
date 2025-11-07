cmake_minimum_required(VERSION 3.10)
project(MoleHole)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
endif()

if (UNIX)
    set(TARGET_BUILD_PLATFORM "linux" CACHE STRING "PhysX target platform")
endif ()
if (WIN32)
    set(TARGET_BUILD_PLATFORM "windows" CACHE STRING "PhysX target platform")
endif ()
set(PHYSX_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/physx/physx" CACHE PATH "Root of the PhysX source tree")
set(PXSHARED_PATH "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/physx/pxshared" CACHE PATH "Path to PxShared")
set(PXSHARED_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" CACHE PATH "PxShared install prefix")
set(CMAKEMODULES_VERSION "1.27" CACHE STRING "CMakeModules version")
set(CMAKEMODULES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/physx/externals/cmakemodules" CACHE PATH "Path to CMakeModules")
set(CMAKEMODULES_NAME "CMakeModules" CACHE STRING "CMakeModules name")
set(PX_OUTPUT_LIB_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE PATH "PhysX library output directory")
set(PX_OUTPUT_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE PATH "PhysX binary output directory")
set(PX_BUILDSNIPPETS OFF CACHE BOOL "Generate PhysX snippets")
set(PX_BUILDPUBLICSAMPLES OFF CACHE BOOL "Generate PhysX samples")
set(PX_GENERATE_STATIC_LIBRARIES ON CACHE BOOL "Generate static PhysX libraries")

add_compile_definitions(PX_PHYSX_STATIC_LIB)

set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
set(GLFW_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

if(MSVC)
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
    set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
    foreach(flag_var
            CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
            CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)
        string(REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
    endforeach()
    set(NV_USE_STATIC_WINCRT ON CACHE BOOL "Use static CRT")
    set(NV_USE_DEBUG_WINCRT ON CACHE BOOL "Use debug CRT")
    set(PX_FLOAT_POINT_PRECISE_MATH OFF CACHE BOOL "Precise math")
    set(PX_WARNINGS_AS_ERRORS OFF CACHE BOOL "Treat warnings as errors")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3" CACHE STRING "CXX flags" FORCE)
endif()

if (WIN32)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/physx/physx/source/compiler/cmake/windows/CMakeLists.txt" PHYSX_CMAKE)
    string(REPLACE "/WX" "" PHYSX_CMAKE "${PHYSX_CMAKE}")
    file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/physx/physx/source/compiler/cmake/windows/CMakeLists.txt" "${PHYSX_CMAKE}")
endif()

add_subdirectory(dependencies/glfw)
add_subdirectory(dependencies/glad/cmake)
add_subdirectory(dependencies/glm)
add_subdirectory(dependencies/spdlog)
add_subdirectory(dependencies/yaml-cpp)
add_subdirectory(dependencies/nativefiledialog-extended)
add_subdirectory(dependencies/imguizmo)
add_subdirectory(dependencies/imgui)
add_subdirectory(dependencies/nodes)
add_subdirectory(dependencies/tinygltf)
add_subdirectory(dependencies/physx/physx/compiler/public)

file(GLOB SOURCES
        src/*.cpp
        src/Application/*.cpp
        src/Simulation/*.cpp
        src/Renderer/*.cpp)

add_executable(MoleHole ${SOURCES})

set_property(TARGET MoleHole PROPERTY CXX_STANDARD 23)
set_property(TARGET MoleHole PROPERTY CXX_STANDARD_REQUIRED ON)

# Define debug/release macros for PhysX compatibility
target_compile_definitions(MoleHole PRIVATE 
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:RelWithDebInfo>:NDEBUG>
    $<$<CONFIG:MinSizeRel>:NDEBUG>
)

include_directories(
    src
    dependencies
    dependencies/glfw/include
    dependencies/glad/include
    dependencies/glm
    dependencies/imgui
    dependencies/imgui/backends
    dependencies/spdlog/include
    dependencies/yaml-cpp/include
    dependencies/nativefiledialog-extended/src/include
    dependencies/stb-image
    dependencies/nodes
    dependencies/physx/physx/include
    dependencies/physx/pxshared/include
)

glad_add_library(glad_gl_core_46 STATIC REPRODUCIBLE LOADER API gl:core=4.6)

target_link_libraries(MoleHole
    glfw
    glad_gl_core_46
    glm
    spdlog::spdlog
    yaml-cpp
    nfd
    ImGuizmo
    imgui
    nodes
    tinygltf
    PhysX
    PhysXCommon
    PhysXFoundation
    PhysXExtensions
    PhysXCooking
)

if(MSVC)
    target_link_libraries(MoleHole
            legacy_stdio_definitions.lib
    )
endif()

if (WIN32)
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(FFMPEG_PKG QUIET libavcodec libavformat libavutil libswscale)
    endif()

    if (FFMPEG_PKG_FOUND)
        include_directories(${FFMPEG_PKG_INCLUDE_DIRS})
        link_directories(${FFMPEG_PKG_LIBRARY_DIRS})
        target_link_libraries(MoleHole ${FFMPEG_PKG_LIBRARIES})
    else()
        if (NOT DEFINED FFMPEG_DIR AND DEFINED ENV{FFMPEG_DIR})
            set(FFMPEG_DIR $ENV{FFMPEG_DIR})
        endif()

        set(_ffmpeg_hints
            ${FFMPEG_DIR}
            "C:/ffmpeg"
            "C:/Program Files/ffmpeg"
        )

        find_path(FFMPEG_INCLUDE_DIR
            NAMES libavcodec/avcodec.h avcodec.h
            HINTS ${_ffmpeg_hints}
            PATH_SUFFIXES include include/ffmpeg
        )

        find_library(AVCODEC_LIB
            NAMES avcodec avcodec.lib libavcodec-58 libavcodec-59
            HINTS ${_ffmpeg_hints}
            PATH_SUFFIXES lib lib64 lib/x64
        )
        find_library(AVFORMAT_LIB
            NAMES avformat avformat.lib libavformat-58 libavformat-59
            HINTS ${_ffmpeg_hints}
            PATH_SUFFIXES lib lib64 lib/x64
        )
        find_library(AVUTIL_LIB
            NAMES avutil avutil.lib libavutil-56 libavutil-57
            HINTS ${_ffmpeg_hints}
            PATH_SUFFIXES lib lib64 lib/x64
        )
        find_library(SWSCALE_LIB
            NAMES swscale swscale.lib libswscale-5 libswscale-6
            HINTS ${_ffmpeg_hints}
            PATH_SUFFIXES lib lib64 lib/x64
        )

        if (FFMPEG_INCLUDE_DIR AND AVCODEC_LIB AND AVFORMAT_LIB AND AVUTIL_LIB)
            include_directories(${FFMPEG_INCLUDE_DIR})
            target_link_libraries(MoleHole ${AVCODEC_LIB} ${AVFORMAT_LIB} ${AVUTIL_LIB} ${SWSCALE_LIB})
            message(STATUS "FFmpeg: found and linked (include=${FFMPEG_INCLUDE_DIR})")
        else()
            message(WARNING "FFmpeg not found!")
        endif()
    endif()
endif()

if (UNIX)
    set(OpenGL_GL_PREFERENCE GLVND)
    find_package(OpenGL REQUIRED)
    target_link_libraries(MoleHole OpenGL::GL)
    target_link_libraries(MoleHole X11 Xrandr Xi Xxf86vm Xcursor Xinerama pthread dl m)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale)
    include_directories(${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(MoleHole ${FFMPEG_LIBRARIES})
endif()
