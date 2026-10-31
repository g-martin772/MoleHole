# MoleHole

C++23 Vulkan application (black hole ray tracing, physics, animation graph) on the `gpp` framework at `dependencies/gpp`.

- Modules only: `.cppm`, `import std`, no headers for own code
- Components and render settings are declared once through descriptors (`RegisterComponent<T>`, `RenderSettings` field plus `BuildRegistry()` entry); UI, YAML and graph pins are generated
- A scene is one YAML file; SI units everywhere, constants only in gpp `core/units/units.cppm`
- `plugins/*.cpp` are hot-reload `.so` UI layers; `src/` is the non-reloaded core
- Minimal comments, match surrounding style
- Build: `cmake --preset vcpkg && cmake --build --preset vcpkg`; tests: `build/molehole_tests`
- See `docs/DEV_GUIDE.md` and `docs/USER_GUIDE.md`
