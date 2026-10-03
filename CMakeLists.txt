cmake_minimum_required(VERSION 4.4)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")

project(MoleHole VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_MODULE_STD 1)
set(CMAKE_CXX_SCAN_FOR_MODULES 1)

add_subdirectory(dependencies/gpp)

file(GLOB_RECURSE MOLEHOLE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE MOLEHOLE_MODULES CONFIGURE_DEPENDS "src/*.cppm")

add_executable(MoleHole ${MOLEHOLE_SOURCES})
target_sources(MoleHole PRIVATE FILE_SET modules TYPE CXX_MODULES FILES ${MOLEHOLE_MODULES})
target_link_libraries(MoleHole PRIVATE gpp)

gpp_add_hot_reload_theme(molehole_theme
        SOURCES plugins/theme.cpp
        OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:MoleHole>")

gpp_add_hot_reload_layer(molehole_viewport_layer
        SOURCES plugins/viewport_layer.cpp
        OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:MoleHole>")

add_dependencies(MoleHole molehole_theme molehole_viewport_layer)
