# vulkan.cmake
# Finds the Vulkan SDK, preferring the local copy in dependencies/vulkan-sdk/
# over any system installation. No environment variables required.
#
# After inclusion, the following are available:
#   Vulkan::Vulkan  — imported target (link with this)
#   VULKAN_INCLUDE_DIRS
#   VULKAN_FOUND

# ── 1. Try the generated cmake_paths.cmake first (written by setup.py) ────────
set(_cmake_paths_file "${CMAKE_SOURCE_DIR}/scripts/cmake_paths.cmake")
if(EXISTS "${_cmake_paths_file}")
    include("${_cmake_paths_file}")
    message(STATUS "Vulkan: using local SDK from scripts/cmake_paths.cmake (${VULKAN_SDK})")
endif()

# ── 2. Build hint list ────────────────────────────────────────────────────────
file(GLOB _local_sdk_versions
    "${CMAKE_SOURCE_DIR}/dependencies/vulkan-sdk/*/x86_64"
)
# Sort so the highest version number is tried first
list(SORT _local_sdk_versions ORDER DESCENDING)

set(_vulkan_hints "")
if(DEFINED VULKAN_SDK AND EXISTS "${VULKAN_SDK}")
    list(APPEND _vulkan_hints "${VULKAN_SDK}")
endif()
foreach(_v ${_local_sdk_versions})
    list(APPEND _vulkan_hints "${_v}")
endforeach()

# ── 3. Locate headers ─────────────────────────────────────────────────────────
find_path(VULKAN_INCLUDE_DIR
    NAMES vulkan/vulkan.h
    HINTS ${_vulkan_hints}
    PATH_SUFFIXES include
    NO_DEFAULT_PATH  # don't fall through to system paths yet
)
if(NOT VULKAN_INCLUDE_DIR)
    find_path(VULKAN_INCLUDE_DIR NAMES vulkan/vulkan.h)
endif()

# ── 4. Locate library ─────────────────────────────────────────────────────────
find_library(VULKAN_LIBRARY
    NAMES vulkan vulkan-1
    HINTS ${_vulkan_hints}
    PATH_SUFFIXES lib lib64
    NO_DEFAULT_PATH
)
if(NOT VULKAN_LIBRARY)
    find_library(VULKAN_LIBRARY NAMES vulkan vulkan-1)
endif()

# ── 5. Create imported target ─────────────────────────────────────────────────
if(VULKAN_INCLUDE_DIR AND VULKAN_LIBRARY)
    set(VULKAN_FOUND TRUE)
    set(VULKAN_INCLUDE_DIRS "${VULKAN_INCLUDE_DIR}")
    set(VULKAN_LIBRARIES "${VULKAN_LIBRARY}")

    if(NOT TARGET Vulkan::Vulkan)
        add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
        set_target_properties(Vulkan::Vulkan PROPERTIES
            IMPORTED_LOCATION             "${VULKAN_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${VULKAN_INCLUDE_DIR}"
        )
    endif()

    message(STATUS "Vulkan: found")
    message(STATUS "  include: ${VULKAN_INCLUDE_DIR}")
    message(STATUS "  library: ${VULKAN_LIBRARY}")
else()
    set(VULKAN_FOUND FALSE)
    message(WARNING "Vulkan SDK not found. Run scripts/setup.py to download it automatically.")
endif()

