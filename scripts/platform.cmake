if(WIN32)
    if(VULKAN_FOUND)
        target_link_libraries(MoleHole Vulkan::Vulkan)
        target_include_directories(MoleHole PRIVATE ${VULKAN_INCLUDE_DIRS})
    endif()
endif()

if(UNIX)
    set(OpenGL_GL_PREFERENCE GLVND)
    find_package(OpenGL REQUIRED)
    target_link_libraries(MoleHole OpenGL::GL)
    target_link_libraries(MoleHole X11 Xrandr Xi Xxf86vm Xcursor Xinerama pthread dl m)

    if(VULKAN_FOUND)
        target_link_libraries(MoleHole Vulkan::Vulkan)
        target_include_directories(MoleHole PRIVATE ${VULKAN_INCLUDE_DIRS})
    endif()
endif()

