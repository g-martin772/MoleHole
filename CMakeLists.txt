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

set(MOLEHOLE_LIB_SOURCES ${MOLEHOLE_SOURCES})
list(REMOVE_ITEM MOLEHOLE_LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

find_package(Catch2 CONFIG REQUIRED)
include(CTest)
include(Catch)

file(GLOB_RECURSE MOLEHOLE_TEST_SOURCES CONFIGURE_DEPENDS "tests/unit/*.cpp")
add_executable(molehole_tests)
target_sources(molehole_tests PRIVATE ${MOLEHOLE_TEST_SOURCES} ${MOLEHOLE_LIB_SOURCES})
target_sources(molehole_tests PRIVATE FILE_SET test_modules TYPE CXX_MODULES FILES ${MOLEHOLE_MODULES})
target_link_libraries(molehole_tests PRIVATE gpp Catch2::Catch2WithMain)
catch_discover_tests(molehole_tests)
