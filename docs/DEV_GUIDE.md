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
- Drop `scripts/foo.luau` → returns `{properties, OnStart, OnTick, ...}` (callbacks below)
- Dir list: `config.json` `GPP.Assets.Scripts`
- Annotate: `OnTick = function(self: ScriptSelf, dt: number)`
- Shared code: `scripts/lib/<name>.luau`, `require("lib/<name>")`. Resolver is `MakeScriptModuleLoader` (luau_bindings.cpp); the VM installs `require` only for loads given a `LuauModuleTrace` (ScriptHost, ScriptCatalog) and `LuauVm::ModulesStale(trace)` drives hot reload of dependents
- Profiling: `ScriptHost::Statuses()[i].Profile` (`ScriptProfile`: tick time, allocs/bytes from `LuauVm::AllocStats()` deltas); shown by the Inspector Scripts section
- Bindings (add Luau API): `gpp/src/scripting/luau_bindings.cpp` (`vm.Register("scene", "name", fn)`)

## Script lifecycle, events, contacts
Host: `ScriptHost` (gpp `src/scripting/script_host.*`), driven in simulations by `ScriptSimulationModule` (`script_module.*`). Callbacks are all optional and take `self` first:

| Callback | When |
|---|---|
| `OnStart(self)` | once per instance (a coroutine: may `task.wait`); again after a hot reload |
| `OnFixedTick(self, dt)` | every sim tick, before `OnTick` (tasks resume first) |
| `OnTick(self, dt)` | every sim tick (PreSim) |
| `OnContact(self, other, point, normal, impulse, relative_velocity, material, other_material)` | a touch began this tick (late pass) |
| `OnTrigger(self, other, entered)` | a body entered/left a trigger collider (late pass) |
| `OnEvent(self, name, ...)` | every event on the bus |
| `OnLateTick(self, dt)` | after every module ticked, so after physics, gravity and geodesics (late pass) |
| `OnStop(self)` | entity or script removed, hot reload, simulation shut down (runner stop, `sim.stop()` in tests) |

- Order per tick: for each instance in (entity guid, slot) order: tasks, `OnFixedTick`, `OnTick`; events drained; all other modules tick; `OnContact`/`OnTrigger` sorted by guid pair (events for the lower guid first, then its partner); events drained; `OnLateTick` per instance; events drained. Scene writes (`scene.set`) apply after each of the two module calls, so a script never sees its own writes within a pass
- Mechanics: `ISimulationModule::OnLateTick` (runner calls it for every module after all `OnTick`s). `ScriptSimulationModule::SetPhysics(physics)` turns on `PhysicsSimulationModule::SetEventCapture` and takes its events after each tick; without it no contact/trigger callbacks fire. Wire it wherever both modules exist (`simulate_cli.cppm` does; the script test runner finds the physics module itself)
- Contacts: `PhysicsContactEvent` (physics_module.cppm) A = lower guid, normal B to A, `RelativeVelocity` = vA - vB before the solver, `Impulse` = summed contact impulse in N s, `Point` world metres, `MaterialA/B` from `Collider.MaterialId`. Scripts get the pair mirrored: `normal` points from `other` toward `self`, `relative_velocity` is self minus other, `material` is self's. Only touch-begin is reported (no persist/lost). The same events are still published on the `EventDispatcher`. Points and velocities reach Luau as float32 vectors (EN-8 widens them)
- Triggers: `Collider.IsTrigger` (existing) makes the shape non-solid and reports enter/exit; PhysX needs one of the pair to be dynamic or kinematic. Both the trigger's and the other body's scripts get `OnTrigger` with the counterpart as `other`
- Events: `events.emit(name, ...)` (args: number, bool, string, vector, entity), `events.on(name, fn)` / `events.off(name, fn)` (inside any callback, handler gets the args, not self), `OnEvent` for everything. Emit only queues; the host drains the queue FIFO at the end of each pass, handlers see events in (entity, slot, registration) order and what they emit queues behind. 4096 events per drain, the rest are dropped with a script error ("events"). Subscriptions die with the instance (`OnStop`, reload, destroy). Events emitted during shutdown reach native subscribers only
- Native side: `ScriptHost::Events()` is a `ScriptEventBus`: `Emit(name, args)`, `Subscribe(name or "", fn)`, `Unsubscribe(token)`, `Dispatch()`. The graph runtime can emit/subscribe there; `events.emit` called from a graph script already lands on the same bus when a `ScriptHost` exists. Sim thread only, not thread-safe
- Replay: scripts record nothing themselves (their input does, see Script input). Contacts are derived from the deterministic physics step, sorted, and delivered in tick order, so a replay re-derives the same callbacks; script-private state (`self` fields, task state) is not in `SaveState`, so replay from a mid-run checkpoint does not restore it (existing limit). Adding `Collider.MaterialId` changed `state_hash` of every scene with a collider once
- GUI: all script execution is sim-owned. `MakeAppSimulationModules` (`src/simulation/app_modules.cppm`) builds the stack the viewport layer and the headless tests share: `GraphSimulationModule`, `ScriptSimulationModule`, gravity, physics, geodesics, N-body, rotation (graphs first so the component-graph names are ignored by the script host before it ticks); `Attach(runner)` wires random, input and the graph->script event forward. `ScriptSimulationModule` (via `MakeScriptModule`, `DeferStart`) runs the script components: they start on the first tick of a Play, never in the paused editor, and `render.get/set`, `scene.spawn/clone/destroy` (guids from the sim clock) are app natives. `GraphSimulationModule` (`graph_module.cppm`) runs the animation and component graphs on the sim thread each tick through `ScriptRuntime` / `GraphSetExecutor` (the UI-process executor is gone; `ScriptRuntime`'s own `ScriptHost` is only used by its unit tests): it reads the graphs from the scene extension (rebuilt when the transpiled sources change or the runtime dropdown flips, `SetUseLuau`), applies writes straight to the sim scene, takes `PostEvent` (key/trigger events from the UI, graph `events.emit` is forwarded to `ScriptSimulationModule::PostEvent`), and publishes errors, statuses and a note. `TraceRecorder` is mutex-guarded and shared through `UiState::GraphTrace`: breakpoints abort the tick on the sim thread, the UI reads. `PlaySession::Stop` calls `Stop(scene)` on both modules before restoring the scene, so `OnStop` runs at Play->Stop and the next Play starts fresh. The UI only displays: `Statuses()`/`TakeErrors()` of both modules go to the inspector and the log, and an asset version change posts `RequestReload()`. Self state: `ScriptSimulationModule::SaveState/LoadState` serialise every instance's `self` fields (Luau serializer in the instance prelude: numbers, bools, strings, vectors, vec2/vec4, entities, nested tables, sorted keys so the state hash is stable) plus the started flag, so `PlaySession` captures, bookmarks and recordings include them; restored instances skip `OnStart`. Not saved: suspended `task.wait` coroutines, functions, metatables other than vec2/vec4, and graph runtime state (variables, latent nodes). Behaviour changes versus the UI-process path: scripts and graphs tick at the sim tick rate with the fixed dt (an export steps the sim, so 30 fps at 60 Hz runs two ticks per frame), graph writes are no longer recorded as commands in `SimulationRecording` (a replay re-executes the graphs instead), and a graph breakpoint no longer pauses script components

## Script input
Gpp: `GPP.Simulation:Input` (`InputFrame`, `SimulationInput`) and `:ActionMap` (`ActionMap`, `ActionRouter`). Luau: `input.axis(action)` (-1..1), `input.held / pressed / released(action)` (held = |value| >= 0.5), `input.mouse_delta()` (vec2 pixels since the last tick, +y down). Unknown actions read 0/false. Declared in `scripts/gpp.d.luau`.
- Flow: device event -> `ActionRouter::SetSource("key:w", 1)` (resolves the `ActionMap` into action values) -> `SimulationInput::Set` (mailbox, any thread; edges are latched at `Set` time, so a tap between two ticks is not lost) -> `SimulationRunner::StepOne` calls `AdvanceInput`: one `SimulationInput::Sample()` per tick, handed to modules through `Input()->Begin(frame)` -> scripts read `Current()` through the natives.
- Recording: the frame is recorded in `SimulationRecording::Inputs` (`InputRecord{Tick, Frame}`) only when the held values change or there are edges/motion; a missing tick keeps the previous held values with no edges. Recording YAML `Inputs:` (format version 2; version 1 files still load). `PrepareReplay` feeds the recorded frames instead of the mailbox until `EndTick`. Recording happens at action level, so rebinding never changes a replay. The wiring a replay needs is `module->SetInput(runner.Input())` in `ReplayOptions::OnRunnerCreated` (`AppSimulationModules::Attach`)
- `SimulationInput::Used()` flips when any script calls an `input.*` native; the UI captures devices only for scenes that read input (`ResetUsed` on `ScriptSimulationModule::Stop`)
- Sources are opaque lowercase strings, the app picks the vocabulary: `key:<name>` (`InputKeyTable` in `src/application/script_input.cppm`: letters, digits, space, enter, tab, arrows, shift/ctrl/alt (either side), f1-f12, punctuation, page/home/end/insert/delete; Esc is never bindable) and `mouse:left|right|middle|x1|x2`. A gamepad adapter only has to call `ActionRouter::SetSource("pad:leftx", v)` and ship bindings that name it; there is none yet
- Bindings text: `key:w`, `-key:a` (scale -1), `0.5*key:w`. An action is the clamped sum of its bindings, so W/-S make one axis. `ActionMap::Defaults()`: `move_x` (D/A), `move_y` (E/Q), `move_z` (W/S), `jump` (Space), `boost` (Shift), `fire` (left mouse), `aim` (right mouse)
- Layers (`EffectiveActionMap`): defaults, then the scene's `Input:` extension (a normal top-level scene section, so still one yaml; `Actions:` mapping of action to a list of bindings, an action replaces the default of the same name), then the user's rebinds (`ScriptInputSettings`, saved as `ScriptInput:` in `.gpp/molehole_state.yaml`, edited in Settings > Script input; a rebind replaces the whole action)
- Capture (`ScriptInputBridge`, core, owned by the viewport layer): events from the `EventDispatcher` reach scripts only while the scene is playing (not paused), its scripts read input, the Viewport window is focused and no text field is active. Esc releases (hotkeys, picking, fly camera work again) until the viewport is clicked; the viewport draws the badge, `UiState::ScriptInputCaptured` makes `HotkeyAllowed` swallow every global shortcut, and the fly-camera keys, right-drag look, scroll speed, gizmo and picking are skipped while captured. There is no relative mouse mode in gpp's window layer yet, so mouse look stops at the window edge
- Headless: `ScriptTestOptions` worlds own a `SimulationInput` sampled once per `sim.step` tick; tests inject with `sim.input(action, value?)` (true/nil = 1, false = 0, held until changed), `sim.input_tap(action)` (press and release before the next tick: pressed and released edges, never held), `sim.input_mouse(dx, dy)` (consumed by the next tick), `sim.input_clear()`

## Test a Luau script
Headless, no GUI. Tests live in `tests/scripts/*.luau` (recursive), one file per script or `scripts/lib/` module.
```sh
build/MoleHole --test-scripts                              # all of tests/scripts
build/MoleHole --test-scripts tests/scripts/orbit.luau     # one file or dir (several allowed)
build/MoleHole --test-scripts --verbose --seed 7           # list passes with timings, other seed
build/molehole_tests "[script-tests]"                      # same files through Catch2 (also part of the full run)
```
Exit code 0 = all passed, 1 = a test or file failed, 2 = bad arguments. Failures print `file:line: message`, the test name and a trace. Run from the repo root (`scripts/` is `--scripts-dir`).

A test file registers tests with `test(name, fn)`. Every test gets a fresh world (empty scene, seeded RNG, new VMs), so tests never leak into each other, and `require("lib/x")` resolves like in a script.
```lua
local orbit = require("lib/orbit")
test("camera sits on the arm", function()
    local target = sim.spawn("Target", { Transform = { Position = vector.create(0, 0, 0) } })
    local cam = sim.spawn("Cam")
    sim.attach(cam, "spring_arm_camera", { Target = target, ArmLength = 10, Smoothing = 0, OrbitSpeed = 0 })
    sim.step(1)
    assert_near(scene.get(cam, "Transform", "Position"), vector.create(0, 3.42, 9.4), 0.01)
end)
```
- Asserts (failure points at the calling line): `assert(cond, msg)`, `assert_eq(a, b, msg?)` (deep for tables), `assert_ne`, `assert_near(a, b, tol=1e-6, msg?)` and `assert_rel` (numbers or vectors, absolute / relative), `expect_error(fn, substring?)` (returns the message; script errors raised inside `sim.step` can be expected too)
- Test world extras: `sim.define_script(name, source)` (per-test script, hot-reloads when redefined), `sim.stop()` (runs `OnStop`, then `sim.step` errors), `sim.emit(name, ...)`, `sim.set_velocity(entity, linear, angular?)` (Velocity fields are read-only to `scene.set`), `sim.input(action, value?)` / `sim.input_tap(action)` / `sim.input_mouse(dx, dy)` / `sim.input_clear()` (script input, see Script input), `sim.events(name?)` (list of `{name, tick, args}` for every bus event so far, in delivery order, tick = 0-based tick index). Physics modules from `options.MakeModules` are wired to the script module, so contact/trigger tests need nothing else (see `tests/scripts/physics_events.luau`)
- World: `sim.spawn(name, {Component = {Field = value}})` (adds missing components, entities start with Transform), `sim.create`, `sim.destroy`, `sim.add_component`, `sim.attach(entity, script, props?)`, `scene.*` (get/set/get_xyz/find/query/...; writes apply immediately)
- Time: `sim.step(n)` ticks all attached scripts (a script error fails the test with its `script:line`), `sim.ticks()`, `sim.dt()`, `sim.set_dt(s)` (default 1/60), `time.elapsed()`, `time.sim_time()`
- Determinism: `math.random` draws from the seeded stream (`sim.seed(n)` restarts it, `--seed` sets the default, 1), `sim.state_hash()` hashes the scene. Never read a wall clock
- Runaway loops hit the VM instruction limit and fail only that test. A file that registers no tests fails
- Headless modules (`MakeHeadlessModules`: gravity, physics, geodesics, N-body, rotation) tick after scripts, so scripts can be tested against bodies
- Reusable runner is gpp `GPP.Scripting:ScriptTest` (`RunScriptTests`, `ScriptTestOptions::MakeModules/Extend`); add natives to the test VM with `Extend`. MoleHole wiring is `src/application/script_test_cli.cppm`
- `--simulate scene.yaml --ticks N --scripts` runs the scene's attached scripts in the headless sim (`ScriptSimulationModule`), exit 1 on script errors

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
