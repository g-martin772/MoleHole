set(GLFW_USE_STATIC_RUNTIME ON  CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS         OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS        OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL            OFF CACHE BOOL "" FORCE)

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

glad_add_library(glad_gl_core_46 STATIC REPRODUCIBLE LOADER API gl:core=4.6)

target_include_directories(MoleHole PRIVATE
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
    target_link_libraries(MoleHole legacy_stdio_definitions.lib)
endif()

