cmake_minimum_required(VERSION 4.4)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")

project(MoleHole VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_MODULE_STD 1)
set(CMAKE_CXX_SCAN_FOR_MODULES 1)

add_subdirectory(dependencies/gpp)

file(GLOB_RECURSE MOLEHOLE_CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE MOLEHOLE_MODULES CONFIGURE_DEPENDS "src/*.cppm")
list(REMOVE_ITEM MOLEHOLE_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(molehole_core SHARED ${MOLEHOLE_CORE_SOURCES})
target_sources(molehole_core PUBLIC FILE_SET modules TYPE CXX_MODULES FILES ${MOLEHOLE_MODULES})
target_link_libraries(molehole_core PUBLIC gpp)

add_executable(MoleHole src/main.cpp)
target_link_libraries(MoleHole PRIVATE molehole_core)

gpp_add_hot_reload_theme(molehole_theme
        SOURCES plugins/theme.cpp
        OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:MoleHole>")

gpp_add_hot_reload_layer(molehole_viewport_layer
        SOURCES plugins/viewport_layer.cpp
        LINK_LIBRARIES molehole_core
        OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:MoleHole>")

set(MOLEHOLE_UI_PLUGINS
        dock_layout_layer
        sidebar_layer
        topbar_layer
        scene_window_layer
        camera_window_layer
        debug_window_layer
        system_window_layer
        viewport_hud_layer
        simulation_controls_layer)

foreach (plugin ${MOLEHOLE_UI_PLUGINS})
    gpp_add_hot_reload_layer(molehole_${plugin}
            SOURCES plugins/${plugin}.cpp
            LINK_LIBRARIES molehole_core
            OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:MoleHole>")
endforeach ()
list(TRANSFORM MOLEHOLE_UI_PLUGINS PREPEND "molehole_" OUTPUT_VARIABLE MOLEHOLE_UI_PLUGIN_TARGETS)

add_dependencies(MoleHole molehole_theme molehole_viewport_layer ${MOLEHOLE_UI_PLUGIN_TARGETS})

find_package(Catch2 CONFIG REQUIRED)
include(CTest)
include(Catch)

file(GLOB_RECURSE MOLEHOLE_TEST_SOURCES CONFIGURE_DEPENDS "tests/unit/*.cpp")
add_executable(molehole_tests ${MOLEHOLE_TEST_SOURCES})
target_link_libraries(molehole_tests PRIVATE molehole_core Catch2::Catch2WithMain)
catch_discover_tests(molehole_tests)
