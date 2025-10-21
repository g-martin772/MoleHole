cmake_minimum_required(VERSION 3.10)
project(MoleHole)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

file(GLOB SOURCES
        src/*.cpp
        src/Application/*.cpp
        src/Simulation/*.cpp
        src/Renderer/*.cpp)

add_executable(MoleHole ${SOURCES})

include_directories(
    src
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
    dependencies
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
