# MoleHole Test Suite
# Requires Catch2 (dependencies/catch2) — added as a git submodule.
#
# Targets:
#   MoleHoleTests       — all unit tests    (ctest -L unit)
#   MoleHoleBenchmarks  — all benchmarks    (ctest -L benchmark)
#
# Usage:
#   cmake -B build -DMOLEHOLE_BUILD_TESTS=ON
#   cmake --build build --target MoleHoleTests MoleHoleBenchmarks
#   ctest --test-dir build --output-on-failure

cmake_minimum_required(VERSION 3.16)
project(MoleHoleTests)

# ── Catch2 ────────────────────────────────────────────────────────────────────

set(CATCH_CONFIG_ENABLE_BENCHMARKING ON CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/catch2 ${CMAKE_BINARY_DIR}/catch2)

include(CTest)
include(Catch)

# ── Shared test helpers library ───────────────────────────────────────────────
# A static lib compiled once and linked into both unit and benchmark targets.
# Add reusable test utilities to tests/helpers/.

file(GLOB_RECURSE TEST_HELPER_SOURCES helpers/*.cpp)

add_library(MoleHoleTestHelpers STATIC ${TEST_HELPER_SOURCES})

target_compile_features(MoleHoleTestHelpers PUBLIC cxx_std_23)

target_include_directories(MoleHoleTestHelpers PUBLIC
    helpers
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/dependencies
    ${CMAKE_SOURCE_DIR}/dependencies/glm
    ${CMAKE_SOURCE_DIR}/dependencies/spdlog/include
)

target_link_libraries(MoleHoleTestHelpers PUBLIC
    Catch2::Catch2WithMain
    glm
    spdlog::spdlog
)

# ── Macro: add_molehole_test ──────────────────────────────────────────────────
# Usage:
#   add_molehole_test(NAME MyTest SOURCES unit/MyTest.cpp [LIBS extra_lib ...])
#
# Creates an executable, links helpers + Catch2, registers with CTest (label: unit).

macro(add_molehole_test)
    cmake_parse_arguments(_T "" "NAME" "SOURCES;LIBS" ${ARGN})

    add_executable(${_T_NAME} ${_T_SOURCES})

    target_compile_features(${_T_NAME} PRIVATE cxx_std_23)

    target_link_libraries(${_T_NAME} PRIVATE
        MoleHoleTestHelpers
        ${_T_LIBS}
    )

    catch_discover_tests(${_T_NAME}
        PROPERTIES LABELS "unit"
    )
endmacro()

# ── Macro: add_molehole_benchmark ─────────────────────────────────────────────
# Usage:
#   add_molehole_benchmark(NAME MyBench SOURCES benchmarks/MyBench.cpp [LIBS extra_lib ...])
#
# Same as add_molehole_test but compiled with optimisations and labelled "benchmark".
# Run manually via the executable directly — Catch2 benchmark output is richer that way:
#   ./build/tests/MyBench --benchmark-samples 100

macro(add_molehole_benchmark)
    cmake_parse_arguments(_B "" "NAME" "SOURCES;LIBS" ${ARGN})

    add_executable(${_B_NAME} ${_B_SOURCES})

    target_compile_features(${_B_NAME} PRIVATE cxx_std_23)

    target_compile_options(${_B_NAME} PRIVATE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O3 -DNDEBUG>
        $<$<CXX_COMPILER_ID:MSVC>:/O2 /DNDEBUG>
    )

    target_compile_definitions(${_B_NAME} PRIVATE CATCH_CONFIG_ENABLE_BENCHMARKING)

    target_link_libraries(${_B_NAME} PRIVATE
        MoleHoleTestHelpers
        ${_B_LIBS}
    )

    catch_discover_tests(${_B_NAME}
        PROPERTIES LABELS "benchmark"
    )
endmacro()

# ── Register tests ────────────────────────────────────────────────────────────

file(GLOB UNIT_SOURCES       unit/*.cpp)
file(GLOB BENCHMARK_SOURCES  benchmarks/*.cpp)

foreach(_src ${UNIT_SOURCES})
    get_filename_component(_name ${_src} NAME_WE)
    add_molehole_test(NAME ${_name} SOURCES ${_src})
endforeach()

foreach(_src ${BENCHMARK_SOURCES})
    get_filename_component(_name ${_src} NAME_WE)
    add_molehole_benchmark(NAME ${_name} SOURCES ${_src})
endforeach()

