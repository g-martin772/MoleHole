# MoleHole

C++23 Vulkan application that simulates and renders black holes (general relativity, ray tracing). Built on the separate `gpp` framework, checked out at `dependencies/gpp`.

## Layout
- `src/` - app code as C++ modules (`.cppm`, `import std`, no headers): `simulation/` components and animation graph, `rendering/` render settings and pipeline, `application/` state and export, `ui/` widget kit
- `plugins/` - one hot-reloadable UI layer per file
- `scripts/` Luau script components, `combos/` graph templates, `templates/` example scenes, `shaders/`
- `tests/unit/` - Catch2; `tests/fixtures/` - test inputs
- `dependencies/gpp/` - reusable engine code: DI, assets, Vulkan graphics, ECS and scene, scripting

## Build and test
- `cmake --preset vcpkg && cmake --build --preset vcpkg` (needs `VCPKG_ROOT`, CMake >= 4.4, Ninja, GCC 16)
- `tools/ci.sh` runs what CI runs; tests that need a GPU are tagged `[gpu]`
- Binary: `build/MoleHole`, run from the repo root

## Conventions
- Components and render settings are declared once through descriptors; UI, YAML and graph pins are generated
- A scene is a single YAML file
- SI units everywhere; constants live in gpp's `core/units/units.cppm`
- Keep comments minimal and match the surrounding style
