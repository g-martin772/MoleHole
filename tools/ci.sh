#!/usr/bin/env bash
# Usage: tools/ci.sh [configure|build|test|format|scenes|bench ...]   (default: all)
# Needs VCPKG_ROOT, CMake >= 4.4, Ninja, GCC 16. GPU tests are tagged [gpu] and skipped.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
root=$PWD

configure()
{
    cmake --preset vcpkg
    (cd dependencies/gpp && cmake --preset vcpkg)
}

build()
{
    cmake --build --preset vcpkg
    (cd dependencies/gpp && cmake --build --preset vcpkg --target unit_tests gpp_test_hot_reload_plugin)
}

test_all()
{
    "$root/build/molehole_tests" "~[gpu]"
    (cd dependencies/gpp/build && ./unit_tests "~[gpu]")
}

format()
{
    if [[ ! -f .clang-format ]]; then echo "format: no .clang-format, skipped"; return 0; fi
    local files
    mapfile -t files < <(git ls-files 'src/*.cpp' 'src/*.cppm' 'plugins/*.cpp' 'tests/*.cpp')
    if clang-format --dry-run --Werror "${files[@]}" 2>/dev/null; then return 0; fi
    echo "format: tree is not clang-format clean"
    [[ "${CI_STRICT_FORMAT:-0}" == 1 ]] && return 1
    return 0
}

scenes()
{
    # exit 1 on errors only; warnings are printed but do not fail
    local scene
    for scene in templates/*.yaml; do
        build/MoleHole --validate-scene "$scene"
    done
}

bench()
{
    local baseline=.gpp/bench-baseline.json
    if [[ ! -f $baseline ]]; then echo "bench: no $baseline, skipped"; return 0; fi
    if tools/bench.py --runs 3 --check "$baseline"; then return 0; fi
    echo "bench: GPU time regressed"
    [[ "${CI_STRICT_BENCH:-0}" == 1 ]] && return 1
    return 0
}

steps=("$@")
[[ ${#steps[@]} -eq 0 ]] && steps=(configure build test format scenes bench)
for step in "${steps[@]}"; do
    echo "== $step =="
    case "$step" in
        test) test_all ;;
        configure | build | format | scenes | bench) "$step" ;;
        *) echo "unknown step: $step" >&2; exit 2 ;;
    esac
done
