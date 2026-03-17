set(GLFW_USE_STATIC_RUNTIME ON  CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS         OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS        OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL            OFF CACHE BOOL "" FORCE)

add_subdirectory(dependencies/glfw)
add_subdirectory(dependencies/glm)
add_subdirectory(dependencies/spdlog)
add_subdirectory(dependencies/yaml-cpp)
add_subdirectory(dependencies/nativefiledialog-extended)
add_subdirectory(dependencies/imguizmo)
add_subdirectory(dependencies/imgui)
add_subdirectory(dependencies/nodes)
add_subdirectory(dependencies/tinygltf)

target_include_directories(MoleHole PRIVATE
    src
    dependencies
    dependencies/glfw/include
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

if(VULKAN_FOUND)
    target_include_directories(MoleHole PRIVATE ${VULKAN_INCLUDE_DIRS})
    target_compile_definitions(MoleHole PRIVATE MOLEHOLE_VULKAN)
endif()

if(SHADERC_FOUND)
    target_compile_definitions(MoleHole PRIVATE MOLEHOLE_SHADERC)
endif()

target_link_libraries(MoleHole
    glfw
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

if(VULKAN_FOUND)
    target_link_libraries(MoleHole Vulkan::Vulkan)
endif()

if(SHADERC_FOUND)
    target_link_libraries(MoleHole shaderc::shaderc_combined)
endif()

if(MSVC)
    target_link_libraries(MoleHole legacy_stdio_definitions.lib)
endif()

