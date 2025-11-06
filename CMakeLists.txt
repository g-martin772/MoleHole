cmake_minimum_required(VERSION 3.10)
project(MoleHole)

# Don't set global C++ standard - let dependencies use their own defaults
# set(CMAKE_CXX_STANDARD 23)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TARGET_BUILD_PLATFORM "linux" CACHE STRING "PhysX target platform")
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
set(NV_USE_STATIC_WINCRT ON CACHE BOOL "Use static CRT")
set(NV_USE_DEBUG_WINCRT ON CACHE BOOL "Use debug CRT")
set(PX_FLOAT_POINT_PRECISE_MATH OFF CACHE BOOL "Precise math")

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

# Set C++23 only for the main MoleHole target
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
