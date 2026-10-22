# MoleHole Dev Guide

## Requirements
- Linux (primary)
- GCC with C++23 `import std` support (tested GCC 16)
- CMake ≥ 4.4
- Ninja
- git, pkg-config, curl, zip, unzip, tar
- Vulkan SDK / drivers
- ffmpeg (video export, optional)
- latex + dvipng (inline latex rendering, optional)
- X11/Wayland dev libs (for SDL3)

## 1. vcpkg
```sh
git clone https://github.com/microsoft/vcpkg ~/Repos/vcpkg
~/Repos/vcpkg/bootstrap-vcpkg.sh
```

## 2. Env
```sh
export VCPKG_ROOT=~/Repos/vcpkg      # add to ~/.zshrc or what ever shell you use
```

## 3. Clone both repos
```sh
cd ~/Repos
git clone https://github.com/g-martin772/MoleHole
git clone https://github.com/g-martin772/gpp
```

## 4. Symlink gpp
```sh
mkdir -p MoleHole/dependencies
ln -s ~/Repos/gpp MoleHole/dependencies/gpp
```
`dependencies/` is untracked

## 5. Build
```sh
cd ~/Repos/MoleHole
cmake --preset vcpkg
cmake --build --preset vcpkg
```
- First run: vcpkg builds all deps (long: PhysX, SDL3, luau…)
- Debug build in `build/`
- Deps listed in `vcpkg.json`

## 6. Run
```sh
./build/MoleHole
./build/MoleHole --scene templates/spring-arm-demo.yaml
```

## 7. Test
```sh
./build/molehole_tests
ctest --test-dir build
```

### Benchmarks
```sh
python3 tools/bench.py --profiles Flat Lensed Traced --width 1920 --height 1080   # GPU ms per profile
python3 tools/bench.py --save-golden .gpp/golden && python3 tools/bench.py --compare .gpp/golden
```
Exports use a static disk, so one machine produces byte-identical images.

## Layout
```
MoleHole/
  src/main.cpp           entry, DI, layers
  src/molehole.cppm      module root
  src/simulation/        components, graph system
  src/rendering/         RenderSettings + profiles, RenderPipeline (stages), sidecar, RenderFrame
  src/application/       app state, ExportController, SceneLoader
  src/ui/                UiState, widgets, theme helpers
  plugins/               hot-reload UI layers
  scripts/  combos/  templates/  shaders/  font/
  tests/unit/
  config.json
gpp/
  src/core/              DI, assets, hotreload, threading, logger
  src/graphics/          vulkan, window, ui backend, shaders
  src/simulation/        scene, ECS, runner, commands, physics
  src/scripting/         Luau VM, bindings, scheduler, script components
  cmake/                 Build scripts
```
- Rule: reusable/backend → gpp. App-specific → MoleHole

## Hot reload
- Layers + theme + viewport = `.so` plugins next to `MoleHole`
- Rebuild target → app reloads (polls 300ms, `config.json`)
- Shaders hot reload too
- `molehole_core` = shared lib, not hot-reloaded since that is basically a full app restart anyway. That includes `RenderPipeline` and its stages (`src/rendering/render_pipeline.cpp`): rebuild and restart after changing them. Shaders still reload live

## Runtime paths and modes
`AppPaths` (gpp) picks the mode from the binary location: *installed* if `<binaryDir>/../../share/<id>` exists (`binaryDir` from `/proc/self/exe`, so the `bin/molehole` symlink resolves to `lib/molehole`), else *dev*.
| | Dev | Installed |
|---|---|---|
| Resources | cwd (repo root) | `<prefix>/share/molehole` |
| Config, state, imgui ini | `.gpp/` | `$XDG_CONFIG_HOME/molehole` |
| Caches, temp | `.gpp/` | `$XDG_CACHE_HOME/molehole` |
| User files (exports, ...) | `.gpp/` | `~/Documents/MoleHole/{scenes,exports,scripts,combos,models}` |
| Hot reload | per `config.json` | off (`packaging/linux/config.json`) |

Config layers, later wins: `<ResourceDir>/config.json` < `<ConfigDir>/config.json` (optional) < in-memory absolute asset dirs (installed only, `InstalledAssetOverrides` in `main.cpp`) < environment < CLI.
- Arrays merge per index: a lower layer with more entries than the override keeps its extra tail. Keep `packaging/linux/config.json` arrays no longer than the overrides in `main.cpp`
- New resource dir: add it to the `install(DIRECTORY ...)` list in `CMakeLists.txt`; new plugins install automatically from `MOLEHOLE_UI_PLUGINS`
- Parallel instances are supported. Plugin shadow copies live in `<CacheDir>/hot-reload-cache/<pid>/` (`ProcessShadowDirectory`), removed at exit; the first use per process sweeps `<pid>` dirs whose process is dead or runs another executable (a crash leaves its dir until the next start). Shared files (state yaml, node editor view, shader cache) are written with `WriteFileAtomic` (temp + rename); `AppStateService::Save` merges the on-disk recents. Other per-process temp: `molehole_export_<pid>_*`, `molehole_latex_<pid>`. File > Launcher... starts another process with `SpawnSelf({"--launcher"})`
- Run an installed copy under `tools/ui_drive.py` with `MOLEHOLE_BIN=<prefix>/bin/molehole` (optional `MOLEHOLE_CWD`)

---

## Register a component
One place: `src/simulation/components.cppm` (`RegisterComponents`).
1. Add struct in the `export namespace`
2. Add `GPP::RegisterComponent<T>("Name", ComponentDescription<T>{ .Fields = {...} })`
```cpp
struct FooComponent { float Power{1}; glm::vec3 Tint{1}; };

GPP::RegisterComponent<FooComponent>("Foo", GPP::ComponentDescription<FooComponent>{
    .Fields = {
        GPP::Field("Power", &FooComponent::Power, {.Min = 0, .Max = 10, .Speed = 0.01f}),
        GPP::Field("Tint",  &FooComponent::Tint,  {.Kind = GPP::FieldKind::Color}),
    }});
```
Auto-generated: YAML load/save, inspector, Add Component menu, graph Get / Decompose / Set nodes, Luau `scene.get/set`.
- `FieldKind`: Plain, Color, Direction, Angle, AssetPath, EntityRef, Multiline
- `FieldMeta`: Label, Category, Min, Max, Speed, ReadOnly, Options, Format, VisibleField, `Dim`, `LegacyScale`, `LegacyDefault`
- SI fields: `double`/`glm::dvec3` member + `.Dim = Units::Dimension::Length|Mass|Time|Velocity|AngularRate` the inspector uses the unit-aware widget
- Description flags: `DisplayName`, `Inspectable`, `GraphExposed`
- New field = one `GPP::Field(...)` line
- Test: `gpp/tests/unit/simulation/component_reflection_tests.cpp`
- Base components (Transform, Camera, RigidBody…): `gpp/src/simulation/components.cppm`

## Units, constants, precision
- One source: `gpp/src/core/units/units.cppm`. Constants are binary128 (`kNameW`) with double views (`kName`)
- SI everywhere. Other systems only through named boundaries
- Name unit-carrying locals: `radiusM`, `massKg`, `periodS`, `rGeo`, `spinStar`
- Display units: widget logic in `src/ui/widget_logic.cppm`, preset (Auto/Human/Stellar/Galactic) in `Units::SetDisplayPreset`,
  per-widget override via right-click in `QuantityValue`. UI symbols stay ASCII (`Msun`, `Rsun`): the font has no ☉
- Shaders get constants as defines: `Units::ShaderDefines()`
- Precision: `Units::Precision` (f32/f64/f128)

## Add a UI layer
1. Create `plugins/foo_layer.cpp`
```cpp
#include <imgui.h>
import GPP; import MoleHole; import std;
#include <gpp/hot_reload_export.h>
using namespace GPP; using namespace MoleHole;

namespace {
struct FooLayer final : public HotReloadableLayer {
    using Dependencies = std::tuple<Logger, UiState>;
    FooLayer(const std::shared_ptr<Logger>& l, std::shared_ptr<UiState> s)
        : HotReloadableLayer(l), m_UiState(std::move(s)) {}
    void OnUiRender() override {
        if (!m_UiState->ShowFooWindow) return;
        if (ImGui::Begin("Foo", &m_UiState->ShowFooWindow)) { /* ui */ }
        ImGui::End();
    }
private: std::shared_ptr<UiState> m_UiState;
};
}
GPP_DEFINE_HOT_RELOAD_LAYER(FooLayer)
```
2. `CMakeLists.txt`: add `foo_layer` to `MOLEHOLE_UI_PLUGINS`
3. `src/main.cpp`: `builder.AddHotReloadableLayer("foo", "molehole_foo_layer.so").SetWindowTarget("main");`
4. `src/ui/ui_state.cppm`: add `bool ShowFooWindow = false;`
5. Toggle: `plugins/sidebar_layer.cpp` button list + `plugins/topbar_layer.cpp` View menu
- Hooks: `OnAttach`, `OnDetach`, `OnUpdate(dt)`, `OnUiRender`
- Widgets: `src/ui/widgets.cppm` (SectionHeader, PropertyRow, SearchBox, DragFloatValue, QuantityValue)
- Theme: `plugins/theme.cpp`
- Cross-thread state: `UiState` (atomics, `OneShot`, `LatestValue`)

## Add a render setting
One place: the field in `RenderSettings` + one `registry.Add(Setting("group.key", &RenderSettings::Field, {...}))` in `BuildRegistry()` (`src/rendering/render_settings.cpp`).
That generates the Render window row (cost badge, tooltip, reset, search), the scene `Render:` YAML, `--set group.key=value`, `--list-settings`, `render.set/get` and the sidecar.
- `SettingMeta`: `Label`, `Group`, `Tooltip`, `Doc`, `Min/Max/Scale`, `Cost` 0-5, `Requires` (a `GpuCapability` key), enum `Options` (+ `OptionLabels`, `OptionRequires`, `OptionCosts`), `VisibleWhen` (`"key"`, `"key=N"`, `"key>=N"`, `"key!=N"`)
- A new setting defaults to `RenderSettings{}`; built-in profiles are in `BuildBuiltIns()`. Keep `Lensed` equal to the defaults (profiles are saved as diffs against their base)
- Consume it in a stage (`render_pipeline.cpp`) and the shader. Uniforms: `RaytraceParamsGpu` in `src/rendering/render_data.cppm` (std140, static_asserts) and the block in `shaders/black_hole_rendering.comp`
- Machinery (generic): `gpp/src/simulation/settings.cppm` (`SettingRegistry<T>`); app side: `RenderProfileSet`, `RenderSettingsService`
- Test: round trips and registry sanity run over every setting (`tests/unit/rendering/render_settings_tests.cpp`)

## Render pipeline
`RenderPipeline::Build` appends passes from an ordered list of `IRenderStage`s: Trace, Bloom, LensFlare, Composite, GravityGrid, PhysicsDebug, ObjectPaths, Meshes. A stage whose `Enabled(settings)` is false records nothing.
- New stage: derive `IRenderStage` in `render_pipeline.cpp`, add it in `RenderPipeline::Initialize` at the right place; `Ready()` reports compiled shaders (exports wait for all)
- GPU timings: `Build` wraps every stage in `graph.BeginScope(name)` / `EndScope()`; passes added in between share the scope. gpp (`GpuProfiler`, `GPP.Graphics:Vulkan.GpuProfiler`) writes timestamps around each scope while the graph executes (sliced passes are summed per segment), and `Renderer::BufferTargetStats::Passes` carries the last frame's `{Name, Ms}` in pipeline order for the stats overlay and the sidecar `GpuPassMs`. Devices without timestamp support (`GpuCapabilities::GpuTimestamps()`) report no passes. Elsewhere `GpuScope scope(profiler, cmd, "name")` brackets raw command recording
- Tracer-resolution targets live in `RenderResources` (`ScaledTraceExtent`); the composite scales them to the viewport
- `ViewportLayer` (`plugins/viewport_layer.cpp`) only owns camera, picking, gizmos and scene switching; export is `ExportController`, loading is `SceneLoader`
- Context: `ViewportLayer::UpdateRenderSettings` picks Edit / Play / Reference / Export each update; edits are mirrored into the scene's `Render:` extension (not undoable)
- GPU features: `VulkanDevice::GetCapabilities()` (gpp); a setting's `Requires` is checked in the UI and `SanitizeForDevice`

## Bodies (planets)
Spheres with a `Surface` are lit, textured planets; spheres without one stay flat-colour markers. Layers are components on the sphere's entity: `Surface`, `Atmosphere`, `CloudLayer`, `Ring`, `Terrain`; `Light` entities light them directly.
- GPU data: `BodyGpu` in `src/rendering/bodies.cppm` (std430, entry i belongs to `u_spheres[i]`), mirrored by `BodyData` in `shaders/body.glsl`. New per-body data: a field in both, filled in `FillBodyData` (lengths and coefficients converted to render units there)
- Textures: `GPP::TextureCache` (`gpp/src/graphics/assets/texture.cppm`) by path and `TextureRole` (Color sRGB / Data / Height R16 / Hdr); mips built on the CPU, hot reloaded, exports wait for them. Bodies get bindless slots via the `slotFor` callback (64 slots, needs descriptor indexing)
- Shading: `shadeBodySurface` (`body.glsl`) takes a normal and a distance, not a world position, because a body in its local frame has none at surface scale
- Layers along a ray: `bodyAllLayers` (atmosphere, clouds, rings, front to back); the tracers call `segmentLayers` per straight segment
- Lighting: per-body sky probes (`GPP_BODY_PROBE` variant of `black_hole_rendering.comp`) reduced by `body_probe_reduce.comp` to SH + one directional source
- Local frame: within 20 radii (`kLocalFrameRadii`) the camera renders the body in its own flat frame and traces the rest from the body's centre
- Notes: `docs/notes/textured_bodies.md`, `body_lighting.md`, `local_frames.md`, `atmospheres.md`, `terrain.md`
- Textures are not in git: `python3 tools/fetch_textures.py` (Earth, Saturn; manifest `tools/texture_manifest.yaml`) and `python3 tools/gen_planet_textures.py` (Miller, Mann, Edmunds). Images picked with the inspector's `...` button are imported into `assets/textures/imported/`

## Solar system scene (N-body)
`templates/solar-system.yaml` is generated, not hand-edited: `tools/fetch_ephemeris.py` (JPL Horizons, cached in `tools/data/`) -> `tools/gen_solar_system.py`, physical data in `tools/solar_system_bodies.py`. Maps: `python3 tools/fetch_textures.py` (group `core`).
- Dynamics: `NBodyComponent` (`GM`, `Velocity`, `J2`) on an entity with a `Transform` and a `Sphere`; `NBodySimulationModule` (`src/simulation/nbody_module.cppm`) integrates all of them in double precision (Newton, 1PN, J2, 6th-order symplectic). It does not use PhysX or `RigidBody`
- Pure functions for tests: `NBodySystem`, `NBodyAcceleration`, `NBodyAdvance`, `NBodyEnergy`; the module is registered in `StartSimulationFor`
- Add a body: an entry in `BODIES` (and a `CAMERAS` row for a follow camera), fetch the ephemeris again, regenerate
- Check an export through a scene camera: `--scene-camera` renders from the camera with `Primary: true`; `gen_solar_system.py --primary <body id>` picks which
- Notes: `docs/notes/solar_system.md`; the 30-day comparison against Horizons is hidden behind `[slow]` (minutes in Debug)

## Talk to the simulation
```cpp
runner.EnqueueCommand(cmd, CommandOptions{.Undoable = runner.IsPaused(), .Label = "Do thing"});
```
- Commands: `gpp/src/simulation/commands.cppm` (serializable, undo history)
- Quick edit: `EnqueueTrackedEdit([](Scene& s){...})`
- `OnlyWhilePlaying = true` for script/graph writes
- Read: published snapshot, never the sim-thread scene

## Add a simulation module
- Implement `GPP::ISimulationModule` (see `src/simulation/gravity_module.cppm`)
- Declare phase; optional `SaveState/LoadState` for Play/Stop restore
- Register where the runner is built (`plugins/viewport_layer.cpp` `StartSimulationFor`)
- Look up: `GetModule<T>()`

## Add a graph node
- Registry: `src/simulation/node_registry.cppm` → `add(name, category, description, keywords, factory)`
- Factory + type: `src/simulation/animation_graph.cppm/.cpp` (`NodeType`, `NodeSubType`)
- Interpreter: `animation_graph_executor.cpp`
- Luau output: `graph_transpiler.cpp`
- Validation: `graph_validation.cppm`
- Mark Luau-only: `IsLuauOnly` in `animation_graph.cpp`
- Component nodes need no code (auto from descriptors)
- Editor UI: `plugins/animation_graph_window_layer.cpp`

## Add a combo
- Drop `combos/foo.yaml` (Name, Keywords, Variables, Fields, Nodes, Links)
- Copy `combos/move_object_to.yaml`
- Dir list: `config.json` `GPP.Assets.Combos`
- Or in editor: Save selection as combo

## Add a Luau script component
- Drop `scripts/foo.luau` → returns `{properties, OnStart, OnTick}`
- Dir list: `config.json` `GPP.Assets.Scripts`
- Annotate: `OnTick = function(self: ScriptSelf, dt: number)`
- Bindings (add Luau API): `gpp/src/scripting/luau_bindings.cpp` (`vm.Register("scene", "name", fn)`)

## Add an asset directory kind
- `AssetDirectories` service: `gpp/src/core/assets`
- Config section `GPP.Assets.<Kind>` with `Directories`, `Extensions`

## Add a CLI flag
- Render settings need none: `--set key=value`
- Other flags: `ParseExportArgs` in `src/main.cpp`
- Request struct: `ExportRequest` in `src/ui/ui_state.cppm`

## Add a test
- `tests/unit/<area>/foo_tests.cpp` (Catch2, auto-picked up on re-configure)
- UI: `tests/unit/ui/*` run real layers headless
- gpp: `../gpp/tests/unit/...`
