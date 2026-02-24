if(UNIX)
    set(TARGET_BUILD_PLATFORM "linux" CACHE STRING "PhysX target platform")
endif()
if(WIN32)
    set(TARGET_BUILD_PLATFORM "windows" CACHE STRING "PhysX target platform")
endif()

set(PHYSX_ROOT_DIR         "${CMAKE_SOURCE_DIR}/dependencies/physx/physx"                               CACHE PATH   "Root of the PhysX source tree")
set(PXSHARED_PATH          "${CMAKE_SOURCE_DIR}/dependencies/physx/pxshared"                            CACHE PATH   "Path to PxShared")
set(PXSHARED_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}"                                                   CACHE PATH   "PxShared install prefix")
set(CMAKEMODULES_VERSION   "1.27"                                                                        CACHE STRING "CMakeModules version")
set(CMAKEMODULES_PATH      "${CMAKE_SOURCE_DIR}/dependencies/physx/externals/cmakemodules"              CACHE PATH   "Path to CMakeModules")
set(CMAKEMODULES_NAME      "CMakeModules"                                                                CACHE STRING "CMakeModules name")
set(PX_OUTPUT_LIB_DIR      "${CMAKE_BINARY_DIR}"                                                        CACHE PATH   "PhysX library output directory")
set(PX_OUTPUT_BIN_DIR      "${CMAKE_BINARY_DIR}"                                                        CACHE PATH   "PhysX binary output directory")
set(PX_BUILDSNIPPETS        OFF CACHE BOOL "Generate PhysX snippets")
set(PX_BUILDPUBLICSAMPLES   OFF CACHE BOOL "Generate PhysX samples")
set(PX_GENERATE_STATIC_LIBRARIES ON CACHE BOOL "Generate static PhysX libraries")
set(PX_COPY_EXTERNAL_DLL    OFF CACHE BOOL "Copy external DLLs" FORCE)

add_compile_definitions(PX_PHYSX_STATIC_LIB)

set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)

if(WIN32)
    file(READ "${CMAKE_SOURCE_DIR}/dependencies/physx/physx/source/compiler/cmake/windows/CMakeLists.txt" PHYSX_CMAKE)
    string(REPLACE "/WX" "" PHYSX_CMAKE "${PHYSX_CMAKE}")
    file(WRITE "${CMAKE_SOURCE_DIR}/dependencies/physx/physx/source/compiler/cmake/windows/CMakeLists.txt" "${PHYSX_CMAKE}")
endif()

if(MSVC)
    set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
    foreach(flag_var
            CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
            CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO)
        string(REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
    endforeach()
endif()

add_subdirectory(dependencies/physx/physx/compiler/public)

# Force PhysX targets to C++11 — they don't compile with newer standards
foreach(_px_target PhysX PhysXPvdSDK PhysXCharacterKinematic PhysXCommon PhysXCooking PhysXExtensions PhysXTask)
    if(TARGET ${_px_target})
        set_property(TARGET ${_px_target} PROPERTY CXX_STANDARD 11)
    endif()
endforeach()

