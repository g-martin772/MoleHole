#!/usr/bin/env bash
# Usage: tools/ci.sh [configure|build|test|format|scenes ...]   (default: all)
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
    # TODO(#4): enable once --validate-scene exists; confirm the CLI shape then.
    if ! grep -rqF -- "validate-scene" src; then echo "scenes: --validate-scene not implemented yet, skipped"; return 0; fi
    local scene
    for scene in templates/*.yaml; do
        build/MoleHole --validate-scene "$scene"
    done
}

steps=("$@")
[[ ${#steps[@]} -eq 0 ]] && steps=(configure build test format scenes)
for step in "${steps[@]}"; do
    echo "== $step =="
    case "$step" in
        test) test_all ;;
        configure | build | format | scenes) "$step" ;;
        *) echo "unknown step: $step" >&2; exit 2 ;;
    esac
done
