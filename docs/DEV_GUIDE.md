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
| `OnContact(self, other, point, normal, impulse, relative_velocity, material, other_material, point64, relative_velocity64)` | a touch began this tick (late pass) |
| `OnTrigger(self, other, entered)` | a body entered/left a trigger collider (late pass) |
| `OnEvent(self, name, ...)` | every event on the bus |
| `OnLateTick(self, dt)` | after every module ticked, so after physics, gravity and geodesics (late pass) |
| `OnStop(self)` | entity or script removed, hot reload, simulation shut down (runner stop, `sim.stop()` in tests) |

- Order per tick: for each instance in (entity guid, slot) order: tasks, `OnFixedTick`, `OnTick`; events drained; all other modules tick; `OnContact`/`OnTrigger` sorted by guid pair (events for the lower guid first, then its partner); events drained; `OnLateTick` per instance; events drained. Scene writes (`scene.set`) apply after each of the two module calls, so a script never sees its own writes within a pass
- Mechanics: `ISimulationModule::OnLateTick` (runner calls it for every module after all `OnTick`s). `ScriptSimulationModule::SetPhysics(physics)` turns on `PhysicsSimulationModule::SetEventCapture` and takes its events after each tick; without it no contact/trigger callbacks fire. Wire it wherever both modules exist (`simulate_cli.cppm` does; the script test runner finds the physics module itself)
- Contacts: `PhysicsContactEvent` (physics_module.cppm) A = lower guid, normal B to A, `RelativeVelocity` = vA - vB before the solver, `Impulse` = summed contact impulse in N s, `Point` world metres, `MaterialA/B` from `Collider.MaterialId`. Scripts get the pair mirrored: `normal` points from `other` toward `self`, `relative_velocity` is self minus other, `material` is self's. Only touch-begin is reported (no persist/lost). The same events are still published on the `EventDispatcher`. `point`, `normal`, `relative_velocity` are float32 vectors; `point64` and `relative_velocity64` (trailing arguments, appended so older callbacks keep working) are the double results as `dvec3`
- Triggers: `Collider.IsTrigger` (existing) makes the shape non-solid and reports enter/exit; PhysX needs one of the pair to be dynamic or kinematic. Both the trigger's and the other body's scripts get `OnTrigger` with the counterpart as `other`
- Events: `events.emit(name, ...)` (args: number, bool, string, vector, dvec3, entity; a `dquat` arrives as a float32 `vec4`), `events.on(name, fn)` / `events.off(name, fn)` (inside any callback, handler gets the args, not self), `OnEvent` for everything. Emit only queues; the host drains the queue FIFO at the end of each pass, handlers see events in (entity, slot, registration) order and what they emit queues behind. 4096 events per drain, the rest are dropped with a script error ("events"). Subscriptions die with the instance (`OnStop`, reload, destroy). Events emitted during shutdown reach native subscribers only
- Native side: `ScriptHost::Events()` is a `ScriptEventBus`: `Emit(name, args)`, `Subscribe(name or "", fn)`, `Unsubscribe(token)`, `Dispatch()`. The graph runtime can emit/subscribe there; `events.emit` called from a graph script already lands on the same bus when a `ScriptHost` exists. Sim thread only, not thread-safe
- Replay: scripts record nothing themselves (their input does, see Script input). Contacts are derived from the deterministic physics step, sorted, and delivered in tick order, so a replay re-derives the same callbacks; script-private state (`self` fields, task state) is not in `SaveState`, so replay from a mid-run checkpoint does not restore it (existing limit). Adding `Collider.MaterialId` changed `state_hash` of every scene with a collider once
- GUI: all script execution is sim-owned. `MakeAppSimulationModules` (`src/simulation/app_modules.cppm`) builds the stack the viewport layer and the headless tests share: `GraphSimulationModule`, `ScriptSimulationModule`, gravity, physics, geodesics, N-body, rotation (graphs first so the component-graph names are ignored by the script host before it ticks); `Attach(runner)` wires random, input and the graph->script event forward. `ScriptSimulationModule` (via `MakeScriptModule`, `DeferStart`) runs the script components: they start on the first tick of a Play, never in the paused editor, and `render.get/set`, `scene.spawn/clone/destroy` (guids from the sim clock) are app natives. `GraphSimulationModule` (`graph_module.cppm`) runs the animation and component graphs on the sim thread each tick through `ScriptRuntime` / `GraphSetExecutor` (the UI-process executor is gone; `ScriptRuntime`'s own `ScriptHost` is only used by its unit tests): it reads the graphs from the scene extension (rebuilt when the transpiled sources change or the runtime dropdown flips, `SetUseLuau`), applies writes straight to the sim scene, takes `PostEvent` (key/trigger events from the UI, graph `events.emit` is forwarded to `ScriptSimulationModule::PostEvent`), and publishes errors, statuses and a note. `TraceRecorder` is mutex-guarded and shared through `UiState::GraphTrace`: breakpoints abort the tick on the sim thread, the UI reads. `PlaySession::Stop` calls `Stop(scene)` on both modules before restoring the scene, so `OnStop` runs at Play->Stop and the next Play starts fresh. The UI only displays: `Statuses()`/`TakeErrors()` of both modules go to the inspector and the log, and an asset version change posts `RequestReload()`. Self state: `ScriptSimulationModule::SaveState/LoadState` serialise every instance's `self` fields (Luau serializer in the instance prelude: numbers, bools, strings, vectors, vec2/vec4, entities, nested tables, sorted keys so the state hash is stable) plus the started flag, so `PlaySession` captures, bookmarks and recordings include them; restored instances skip `OnStart`. Not saved: suspended `task.wait` coroutines, functions, metatables other than vec2/vec4, and graph runtime state (variables, latent nodes). Behaviour changes versus the UI-process path: scripts and graphs tick at the sim tick rate with the fixed dt (an export steps the sim, so 30 fps at 60 Hz runs two ticks per frame), graph writes are no longer recorded as commands in `SimulationRecording` (a replay re-executes the graphs instead), and a graph breakpoint no longer pauses script components

## Script physics API
Luau `physics` table, installed by `RegisterPhysicsBindings` (gpp `src/scripting/physics_bindings.*`) in `ScriptSimulationModule` (backed by `SetPhysics`) and in the headless test world. Declared in `scripts/gpp.d.luau`. Sim thread only, metres/SI, vector arguments are Luau vectors, `dvec3` or `{x, y, z}` double tables (a vector is float32, so pass a `dvec3` for positions beyond ~1e6 m; `hit.point64` is a `dvec3`, `hit.point` the float vector).
- Queries (`PhysicsSimulationModule::Raycast/Spherecast/Overlap`): `physics.raycast(origin, dir, max_dist, [layer_mask])` and `physics.spherecast(origin, radius, dir, max_dist, [mask])` return `{entity, point, normal, distance, point64}` or nil (closest blocking hit, a start inside a body hits at distance 0); `physics.overlap(center, radius, [mask])` returns entities sorted by guid (at most 512). Triggers are ignored by queries. Queries see the PhysX scene as left by the previous tick's step, plus entities and moved transforms the scene holds now (each query first syncs actors, so bodies spawned this tick are hit). Before `PhysicsSimulationModule::OnInit` (script `OnStart` on the first tick) they return nil
- Layers: `Collider.Layer` (0..31, default 0) and `Collider.Mask` (bit i = collides with layer i, -1 = all). Two shapes collide when each one's layer is in the other's mask (`DefaultFilterShader`, set at actor creation, so like Shape they apply on scene reload). Query `layer_mask` selects layers by the same bit rule; 0 matches nothing
- Forces (`PhysicsCommand` queue): `add_force` (N, applied for the one step), `add_torque`, `add_impulse(e, J, [point])` (N s, point in world metres), `set_velocity`, `set_angular_velocity`, `set_kinematic_target(e, pos, [euler_degrees | dquat])`, all on the entity's rigid body (Dynamic, except the kinematic target needing a Kinematic one). The call validates and queues; `PhysicsSimulationModule::OnTick` drains the queue in call order after `SyncActors` and right before the PhysX step. A script `OnFixedTick`/`OnTick` (PreSim) therefore acts on the step of the same tick, while `OnContact`/`OnTrigger`/`OnLateTick` calls wait for the next tick's step. Reading `Velocity`/`Transform` after an `add_*` in the same pass still shows the old value. A kinematic target is moved by PhysX (it pushes dynamic bodies) and written to `Transform` exactly (double) after the step
- `physics.set_gravity_scale(e, s)` writes `RigidBody.GravityScale` through the normal scene write (the app's `GravitySimulationModule` multiplies the Newtonian acceleration by it; plain PhysX gravity is zero)
- Joints: `physics.create_joint(a, b|nil, "fixed"|"hinge"|"distance", params)` returns a number handle, `break_joint(h)` (true if it existed), `joint_broken(h)` (nil for unknown handles). `params`: `anchor` (world point, default a's position, hinge pivot), `axis` (hinge, default x), `distance` (max length, default current), `min_distance`, `stiffness`/`damping` (spring), `break_force`/`break_torque` (0 = unbreakable). b nil joins a to the world. At least one body must be Dynamic. The module keeps `JointSpec` (local frames, so a restored joint keeps its relative pose) and serialises them in `PhysicsSimulationModule::SaveState/LoadState`, so Play/Stop snapshots, bookmarks and replays recreate them; joints die with their actors. A joint broken by PhysX is released at the next tick start and reports `joint_broken == true`
- Errors are Luau errors: `physics.add_force: entity 'Floor' must be a Dynamic body`, `... has no RigidBody and Collider`, `expected an entity`, `force must be a vector or {x, y, z} (a dvec3 works too)`, non-finite vectors, bad ranges, unknown joint kinds or parameters
- Local origin: the module keeps its existing fixed origin (mean position of the bodies at `OnInit`) and converts every world double to PhysX float relative to it, so queries and commands are exact for bodies clustered within float range of each other even 1e11 m from the world origin (tests cover raycast, spherecast, overlap and an impulse at 1e11). Bodies drifting far from that origin lose float precision (the origin is not re-based yet)
- Determinism: commands and queries run in script order on the sim thread, so a replay re-derives them; `state_hash` is equal across runs (`script_physics_tests.cpp`, `tests/scripts/physics_api.luau`)
- Example: `scripts/ray_gun.luau` (fire action ray-casts and pushes the hit body at the hit point)

## Script 64-bit math
`dvec3` and `dquat` (gpp `src/scripting/dmath_bindings.cpp`, installed by `RegisterMathBindings`, so every script VM has them) keep double precision where Luau's native `vector` is float32 (about 7 digits: a ship 1e11 m out jitters by kilometres). Declared in `scripts/gpp.d.luau` (`DVec3`, `DQuat`); convenience helpers in `scripts/lib/dvec.luau` and `scripts/lib/dquat.luau`.
- Representation: tagged Luau userdata (tags 21 and 22) holding 3 or 4 plain doubles, metatable installed with `lua_setuserdatametatable` (`typeof` gives `"dvec3"`/`"dquat"`, `getmetatable` is locked), `x/y/z/w` reads go through the direct-field API (no call frame), methods through a fixed `__index`. Values are immutable (writes are errors); every operation builds a new userdata, no uninitialised bytes, no hashing or ordering, all math is plain IEEE double with the same code path on every run, so replays and `state_hash` are unaffected (nothing here writes sim state)
- `dvec3`: `new(x, y, z)` (or `new()`), `from_vec`, `to_vec`, `unpack`, `dot`, `cross`, `length`, `length_squared`, `normalize` (zero stays zero), `distance`, `lerp`, plus `+ - * / unary- == #` (`*` and `/` take a number or another dvec3, component-wise) and `tostring`. Operators and functions are strict: mixing with a float32 `vector` or a number where a dvec3 is needed is an error naming the types (`dvec3.from_vec` converts on purpose); inputs documented as "or vector" (`from_axis_angle`, `from_euler`, `from_vec`, physics and `scene.set64`) widen exactly
- `dquat` (x, y, z, w, unit quaternions, rotation about +y is `from_axis_angle(dvec3.new(0, 1, 0), radians)`): `new`, `from_axis_angle`, `from_euler` (degrees, same convention as the scene `Rotation` field), `to_euler`, `mul`, `rotate`, `slerp` (shortest arc), `inverse`, `conjugate`, `normalize`, `dot`, `length`, `to_quat`/`from_quat` (float32 `vec4`), and `q * q`, `q * dvec3`. `lib/dquat` adds `from_to`, `look_rotation` (identity faces -Z, +Y up), `angle`, `rotate_towards`, `forward/up/right`
- Scene access: `scene.get64(e, comp, field)` returns a `dvec3` for `DVec3`/`Vec3` fields (position, velocity, angular velocity, scale), a `dquat` for the angle-kind field (`Transform.Rotation`), a number or the usual value otherwise. `scene.set64` takes the same types (a vector or dvec3 for vector fields, dquat or euler-degrees dvec3 for rotation) and raises `scene.set64: Transform.Position needs a dvec3 or vector, got number` on a wrong type; it returns false for an unknown field, a read-only field or no writer. Both go through `FieldInfo::Get/Set` and `MakeComponentFieldWrite` / `SetFieldCommand` exactly like `scene.get/set` (exclusive flags, undo and command logging unchanged). `scene.set(e, comp, field, dvec3)` also accepts a dvec3 and keeps the double for `DVec3` fields; `scene.get/set` with float32 vectors are unchanged. Storage limits stay: `Transform.Rotation` and `Scale` are float32 in the component, so only position-like double fields gain precision
- Natives: `LuauNativeCall::ToDVec3/ToDQuat/PushDVec3/PushDQuat/IsDVec3/IsDQuat/TypeName`; `ToValue(DVec3)` and `ToValue(Vec3)` accept a dvec3; `ToValueAuto` maps a dvec3 to the `glm::dvec3` alternative of `LuauValue` and a dquat to a `vec4`. `PushValue` still narrows a `glm::dvec3` to a float32 vector (scene.get, graph runtime); `PushValueWide` (used by `CallWith` arguments and `PushValues`) pushes a dvec3, so events keep their precision from `events.emit` to `OnEvent`
- Physics: raycast/spherecast `point64` is a `dvec3`; every vector argument accepts a `dvec3` (the shared `xyz` helper in the physics prelude); `set_kinematic_target` takes a `dquat` rotation directly. `OnContact` gets `point64`/`relative_velocity64` after `other_material`
- Tests: gpp `tests/unit/scripting/script_dmath_tests.cpp` (operators, precision, errors, get64/set64, events, determinism) plus cases in `script_physics_tests.cpp`/`script_lifecycle_tests.cpp`; headless `tests/scripts/dvec.luau` and `tests/scripts/far_origin.luau` (a body at 1e11 m integrated with get64/set64 stays within a millimetre over 300 ticks, the float32 path is kilometres off)

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

## Script library
Pure Luau in `scripts/lib/`, `require("lib/name")`, all functions of explicit arguments (time comes from `dt` or `time.sim_time()`, never a wall clock; `scripts/**` is linted for `os`, `math.randomseed`, `Random.new`, `tick()`). Numbers, `vector` (float32) and `dvec3` work wherever a value is interpolated; `dquat` slerps. Tests: `tests/scripts/<module>.luau`; the lint and the SaveState round trip are in `tests/unit/scripting/script_lib_tests.cpp`.
- State that must survive `SaveState` is plain data (numbers, strings, bools, `vector`, nested tables; `dvec3`/`dquat`/functions are not saved, depth limit 12). Keep module state as a direct field of `self` (`self.pid`), build handler tables in `OnTick`, and check with `plain.check(state)`
- `lib/curves`: `ease(name, t)` (`linear smoothstep smootherstep in_/out_/in_out_ quad cubic sine`, plus the engine names `easeIn cubicOut ...`), `lerp inverse_lerp remap clamp01`, `bezier(points, t)` / `bezier_tangent`, `hermite(p0, m0, p1, m1, t)` / `hermite_tangent`, `spline(points, {closed, tension})` with `spline_eval/spline_tangent(s, u)` (u in 0..segments), constant speed via `spline_table(s)` + `spline_at_length(s, arc, distance)` (`arc_table/u_at_length/length_at_u` for any function), `track(keys, {wrap})` + `track_eval(track, t)` (key `mode`: `step`, `cubic` or any easing; wrap `clamp|loop|pingpong`), `damp(current, target, lambda, dt)`, `smooth_damp(current, target, velocity, smooth_time, dt, max_speed?)` returning `value, velocity`. Example: `curves.track_eval(curves.track({ { t = 0, v = 0, mode = "smoothstep" }, { t = 2, v = 10 } }), time.sim_time())`
- `lib/pid`: `new(gains)` / `new_vec(gains)` (`kp ki kd out_min out_max i_limit d_tau`), `step(state, setpoint, measurement, dt, rate?)`, `step_vec(...)` (vector or dvec3 in and out), `reset`. Derivative on the measurement (or the supplied `rate`), integral clamped to the output headroom and to `i_limit`, `dt <= 0` is a no-op. Example: `self.pid = pid.new({ kp = 8, kd = 3, out_max = 5, out_min = -5 })` then `local u = pid.step(self.pid, target, self.speed, dt)`
- `lib/dvec` additions: `set_length perpendicular signed_angle rotate_towards(from, to, max_radians, max_length)`, `damp`, `smooth_damp`; `lib/dquat` additions: `to_rotation_vector from_rotation_vector delta(from, to)` (world-frame rotation vector) `integrate(q, omega, dt) look_at damp smooth_damp(current, target, angular_velocity, smooth_time, dt, max_speed?)`; `dquat.angle` is now exact near zero. Example: `self.q, self.w = dquat_lib.smooth_damp(self.q, dquat_lib.look_at(pos, target), self.w, 0.3, dt)`
- `lib/input_map`: `new({axes, toggles, repeats, sticks})`, `update(state, dt, source?)` once per tick (source defaults to the engine `input`), then `axis(state, name)`, `stick(state, name)` (x, y), `toggled`, `set_toggle`, `repeats` (fires this tick). Axis: `action` or `positive/negative`, `deadzone expo sensitivity invert slew`; stick: `x`/`y` actions with a radial deadzone and diagonal normalisation; pure `shape(x, opts)` and `stick_shape(x, y, opts)`. Example: `input_map.update(self.map, dt); local x, y = input_map.stick(self.map, "move")`
- `lib/sequencer`: nodes `wait call(name, ...) tween(name, from, to, duration, ease?) sequence{} parallel{} loop(node, count?) every(period, name, ...)`, `new()`, `add(state, node, {at, after, tag})` (also `at`, `after`), `update(state, handlers, dt)`, `cancel(state, id_or_tag)`, `clear`, `active`. Handlers are functions looked up by name (`handlers.name(args...)`, tweens get `(value, u)`); events fire in order within a cue at tick granularity; nesting is limited to 7 levels so the state fits the save depth. Example: `sequencer.add(self.seq, sequencer.sequence({ sequencer.wait(1), sequencer.call("log", "go") }))` and `sequencer.update(self.seq, { log = print }, dt)`
- `lib/plain`: `check(value, name?)` returns `ok, offender`, `copy(value)`. Example: `assert(plain.check(self.seq, "self.seq"))`

## Flying a ship
`scripts/ship_controller.luau` (flight model `scripts/lib/flight.luau`), the camera rigs `chase_camera`, `cockpit_camera`, `fly_camera`, `frame_camera` (`lib/camera_cycle`) and the flight HUD `telemetry_hud` (`lib/hud`, `ui.hud`); demo `templates/fly-ship.yaml`.
- Attach: `ship_controller` goes on an entity with `Transform`, `Velocity`, `RigidBody` (Dynamic) and a `Collider` (inertia is read from a Box/Sphere/Capsule collider unless `Inertia` is set). Each camera rig goes on a `Camera` entity with `Target` = the ship and `Next` = the next camera of the ring (chase -> cockpit -> fly -> frame -> chase); the first has `Primary: true`. The mesh must have its nose along the entity's local -Z (the demo mirrors the Kelvin model with `Scale: [-1, 1, -1]`)
- Units: everything is SI per physical second. Physics steps `dt * TimeScale`, so thrust is `time.scale()` times weaker per sim second and the controller hands `flight` that physical dt. `fly-ship.yaml` uses `TimeScale: 0.01` and numbers sized for a 500 r_g orbit around a solar-mass hole (circular speed 1.34e7 m/s, period 0.346 s = 35 sim seconds)
- `ship_controller` properties: `ThrustX/Y/Z` (N, right / up / forward, same both ways), `TorquePitch/Yaw/Roll` (N m), `MaxRate` (deg/s, keep under PhysX's 100 rad/s clamp), `ResponseTime` (s, rate and attitude loops), `Assist` (0 off, 1 rate damping, 2 velocity + attitude hold, 3 retro brake), `MassOverride` (kg, `0` = body mass), `Inertia` (vec3, `0` = from the collider), `GLimit` (g of thrust acceleration, `0` = off), `Boost`, `RetroStop` (m/s), `MouseSensitivity`, `InvertPitch`, `ControlMode` (initial: 0 mouse steer, 1 keys only), `KeyTurn` (0..1 scale of keyboard pitch/yaw/roll), `KeySlew` (1/s ramp of the keyboard axes, `0` = instant), `MaxSpeed` (m/s thrust governor, `0` = off; `0.1 c` = 2.998e7), `ThrottleRate` (setting per sim second while Z / X is held), `ThrottleMin` (`0`, or down to `-1` for a reverse range)
- Actions read (scene `Input:` binds them; built-ins cover `move_x/z` and `boost`): `move_x/y/z`, `pitch` (+ nose up), `yaw` (+ nose right), `roll` (+ right wing down), `boost`, `assist`, `control_mode`, `throttle` (+ raises, held), `throttle_cut`, `speed_hold`, `kill_velocity` (held), `camera`, `hud`, `cam_yaw`, `cam_pitch` (frame camera orbit). `fly-ship.yaml` binds them as in the table; everything is rebindable in the scene `Input:` section or Settings > Script input

| Keys | Action |
|---|---|
| W / S, A / D, Space / Ctrl | forward / back, strafe, up / down thrust |
| Shift | boost |
| Arrows or I / K, J / L | pitch, yaw (I and Up = nose up) |
| Q / E | roll |
| Mouse | pitch and yaw (mouse steer mode only) |
| M | toggle mouse steer / keys only (the mouse is ignored in keys only) |
| Z / X | raise / lower the persistent throttle (0..1 of `ThrustZ`, reverse with `ThrottleMin`) |
| R | cut the throttle to zero |
| V | speed hold: capture the current speed and hold it (PID along the velocity); throttle keys, kill velocity or pressing V again drop it; W / S override it while held |
| B (hold) | kill velocity: all thrusters oppose the velocity whatever way the nose points |
| F | assist off / rate / hold / retro |
| C | camera: chase, cockpit, free-fly, frame lock |
| . / , and ; / / | orbit the frame-lock camera (mouse also, in keys only mode) |
| H | HUD full / compact / off |

- Cameras: the rig holding `Camera.Primary` runs, the others idle. C writes `Primary` on `Next` and emits `camera_switch(from, to)`; the rig that becomes primary emits `camera_mode(name)` (`chase`, `cockpit`, `fly`, `frame`). The ship ignores input while the mode is `fly`. `chase_camera` smooths its offset to the ship and its aim; `fly_camera` starts at the previous pose and co-moves with its `Target`. `frame_camera` is the fixed-orientation rig: it keeps its own world-fixed yaw/pitch (taken from the previous camera on a switch, orbited with `cam_yaw`/`cam_pitch`, plus the mouse while the ship is in keys-only mode) and only its position follows the ship: `curves.smooth_damp` on the offset to the ship, `Distance` behind the view axis and `Height` along the view's up, so the ship turns inside a steady view at any speed. `StartActive` makes it the primary camera at Play. Offsets are float32 vectors, positions go through `scene.set64`. Rigs run in `OnLateTick`, after physics
- Events: the controller emits `assist_mode(n)` on change and `ship_status(throttle, assist, boost, g, control_mode, hold_speed, killing)` every tick (the HUD and the frame camera listen)
- `lib/flight`: `new(config)`, `configure(state, config)`, `step(state, body, command, dt) -> force, torque, info` (pure, world-frame `dvec3`s; `body = {mass, rotation, velocity, angular_velocity}`, `command = {x, y, z, pitch, yaw, roll, boost, throttle, kill}`, axes in -1..1, `throttle` adds to `z`, `kill` overrides everything else), `apply(entity, state, command, dt)`, `read`, `collider_inertia`, `box_inertia`, `sphere_inertia`; assist constants `OFF RATE HOLD RETRO`. Config keys: `thrust_x/y/z torque_x/y/z inertia_x/y/z max_rate response mass g_limit boost retro_stop assist max_speed hold_speed` (`hold_speed < 0` = off). State is plain data (pid state per axis and for the speed hold, held velocity and attitude), safe as `self.flight`
- Control law: assist off applies raw torque only while a rotation is commanded. Otherwise per-axis `lib/pid` (kp = I / response, ki = kp / 4 response) tracks a body rate: the commanded one, zero, or `error / response` toward the held attitude or the retrograde attitude. Velocity hold applies `m dv / max(dt, response)` while no thrust is commanded; retro thrusts forward once the nose is within 8 degrees of retrograde. Speed hold (`hold_speed`) runs a PID on `|v|` (kp = m / response, critically damped ki) and pushes along the velocity (along the nose from rest), clamped per body axis; kill velocity is `m (-v) / max(dt, response)` clamped per axis; `max_speed` rescales the predicted velocity so thrust cannot raise `|v|` past the cap (a faster ship is only stopped from speeding up further); the G limit applies after all of them
- Tests: `tests/scripts/flight.luau`, `ship_controller.luau`, `ship_cameras.luau`, `hud.luau`; `tests/unit/simulation/fly_ship_tests.cpp` loads the template with the app module stack, steps it, cycles the camera, checks the published HUD and replays recorded input (throttle, speed hold, kill, control mode, camera orbit, HUD toggle) twice to the same hash
- Not yet: no autopilot, no fuel; no gamepad (no pad adapter); mouse look stops at the window edge; the HUD shows gamma from the speed only (`host.clockrate` for dtau/dt exists only in the graph runtime, not in script components)

## Script HUD
`ui.hud` (gpp `src/scripting/hud_bindings.*`, declared in `scripts/gpp.d.luau`) draws immediate-mode 2D overlays in the Viewport window from script code; `scripts/lib/hud.luau` has readouts and drawing helpers, `scripts/telemetry_hud.luau` the flight HUD.
- Draw: `ui.hud.text(x, y, str, {size, color, align})` (`size` in px of a 720 px tall viewport, scaled with the real height; `y` is the text's vertical centre; `align` left/center/right), `line(x1, y1, x2, y2, {color, thickness})`, `rect(x, y, w, h, {color, filled, thickness})`, `circle(x, y, radius, {color, filled, thickness})`, `arc(x, y, radius, from_deg, to_deg, {color, thickness})` (0 = up, clockwise), `clear()` (drops everything queued this tick), `color(r, g, b, a?)`. Colors: `{r, g, b, a}` (0..1, also `{1, 0.5, 0}`), `"#rrggbb"` / `"#rrggbbaa"`, `0xRRGGBB` or `0xRRGGBBAA`, a name (`white red green blue yellow orange cyan magenta gray black`), or a vector. Thickness is in px
- Space: coordinates are fractions of the viewport (`x` of the width, `y` of the height, 0,0 = top left), so a HUD scales with the window. `ui.hud.at(space)` returns the same API bound to another space: `"left"`, `"center"`, `"right"` use units of the viewport height for both axes (round things stay round at any aspect) with x measured from the left edge, the centre or the right edge (negative x = inwards from the right), y from the top; or pass `space = ...` per call. Radii and rect sizes follow the space (`screen`: w by width, h and radii by height)
- Lifetime: the list is rebuilt every tick: it is cleared when the script module ticks, scripts re-emit from `OnTick`/`OnLateTick`, and the result is published after the late tick. Nothing is drawn after Stop or when scripts stop emitting; a paused sim keeps the last frame. At most 4096 commands per tick (all scripts together) or the call raises `ui.hud: more than 4096 commands in one frame`; text is cut at 256 bytes. Bad colors, spaces or non-finite numbers raise `ui.hud.<fn>: ...`
- Threads and determinism: scripts write into a sim-thread list, `ScriptSimulationModule::HudSnapshot()` returns the last published `shared_ptr<const HudFrame>` (mutex-guarded swap), `viewport_hud_layer` reads it per UI frame and draws it with the Viewport window draw list clipped to the image, so popups cover it. HUD calls touch no scene, `SaveState` or hash. The viewport input badge is bottom-left, render warnings bottom-right, so the flight HUD keeps its stacks above 0.9
- Tests: headless `sim.hud()` returns the published list as `{kind = "text"|"line"|"rect"|"circle"|"arc", space, x, y, ..., color = {r, g, b, a}}` (`tests/scripts/hud.luau`); gpp `tests/unit/scripting/script_hud_tests.cpp` (content, cap error, clear on stop, state hash, thread safety)
- `lib/hud`: `format_speed/format_beta/format_distance/format_angle/format_accel`, `gamma(speed)`, `text_gauge(value, lo, hi, width)`, `attitude(q)` (forward/up/right, `heading` 0 = -Z toward +X, `pitch`, `roll` + right wing down), `to_body`, `project(direction, camera_q, fov)` (centre-space point, edge angle, in front), `body_marker`, `nearest_body(position, exclude)` (BlackHole and Sphere entities: distance, altitude, distance in r_g), and drawing on any surface table with the `ui.hud` shape: `bar`, `vbar`, `marker(prograde|retrograde|target)`, `edge_arrow`, `attitude_ball`, `triad`, `fade`
- `telemetry_hud` properties: `Subject` (default the entity it sits on), `Reference` (relative speed), `Target` (marker, range, closing speed), `Level` (initial: 2 full, 1 compact, 0 off; the `hud` action cycles it), `Scale` (text size), `Aspect` (only decides when a marker counts as on screen), `Attitude`, `Markers`, `Environment` (nearest body). It shows: speed in the scene frame with c fraction and gamma, relative speed, felt g and acceleration, body rates, nearest body distance, altitude and r_g, throttle bar, assist, boost, speed hold or kill state, camera and control mode, an artificial horizon with pitch ladder and the velocity and retrograde markers, a world-axis triad, heading / pitch / roll, and screen markers for prograde, retrograde (green, red) and the target (cyan, an edge arrow when out of view)

## Waypoint paths
`scripts/waypoint_path.luau` (curves from `lib/curves`, orientation from `lib/dquat`); demo `templates/waypoint-demo.yaml`. Add Script `waypoint_path` to any entity with a `Transform` (a Camera entity makes a rig). Do not put it on a Dynamic rigid body: physics would fight the writes.
- Pure function of the scene clock: every tick it evaluates position, orientation and field of view at `time.elapsed() - StartTime` and writes them, so scrubbing, pause, a clock jump, Play/Stop, replay and export all give the same pose. The only `self` fields are `lap`, `done`, the waypoint entity list, the last waypoint coordinates and the spec string (plain data); the evaluator is a closure in `self.evaluate`, which `SaveState` skips and the next tick rebuilds. The script sees the clock before it advances, so the pose after tick `n` is the one for `(n-1) * dt`
- Waypoints: `Waypoints` is a `;` separated list of entity names or decimal guids (names may hold spaces; resolved once per change of the string, positions are re-read every tick so moving markers re-shape the path), or `Points` is inline `x y z; x y z; ...` in metres (strings keep the full double, the float32 `vec3` property type would not). `Waypoints` wins when both are set. The spline is a Catmull-Rom through the points (`Tension` -1..1, 0 = Catmull-Rom, 1 = zero tangents so the curve eases through every waypoint; `Closed` joins the last point to the first), evaluated relative to the first waypoint in `dvec3`, so 1e11 m coordinates stay within a millimetre
- Timing, in this order of precedence: `TimeLaw` = `time waypoint [easing]; ...` (waypoint index is 0-based and may be fractional; easing applies on the segment leaving the key, any `lib/curves` easing; holds the first value before and the last after), else `Duration` seconds for the whole path (`Ease` bends the progress), else constant `Speed` in m/s along the arc length (arc table of 64 samples per segment)
- `Mode`: 0 once (stops on the last waypoint and emits `waypoint_path_done(name)` once; scrubbing back re-arms it), 1 loop, 2 pingpong (`waypoint_path_lap(name, pass)` is emitted each time a new pass starts). `Name` is the event label, default the entity name. Use `Closed` with loop and a time law ending at waypoint `n` for a seamless circuit. Receive with `OnEvent(self, name, label)` or `events.on`
- Orientation, first match wins: `Orientation` keys `time pitch yaw roll [easing]; ...` (euler degrees, same convention as the scene `Rotation`, slerped; clamps for once, wraps for loop and pingpong), else `Target` (entity to look at, +Y up), else `FaceTravel` (look along the path), else untouched. `Transform.Rotation` is stored as float32 euler degrees, so a yaw of exactly +-90 degrees costs about 3e-4 rad
- Camera rig: on an entity with a `Camera`, `FovKeys` = `time fov [easing]; ...` writes `Camera.Fov`, `MakePrimary` makes it the primary camera when Play starts. Chain rigs by pointing one path's `Target` at another path's entity (the demo's camera looks at a `Focus` marker that itself glides from Earth to the Moon)
- Errors (fewer than 2 points, bad number, unknown name, time law past the last waypoint, destroyed waypoint) are script errors naming the problem
- Tests: `tests/scripts/waypoint_path.luau` (constant speed per tick, time law hits, easing, clock jumps, 1e11 m, loop/pingpong/once and events, orientation endpoints, target, face travel, FOV, errors, guids); `tests/unit/simulation/waypoint_demo_tests.cpp` loads the template with the app module stack (no script errors, gate times, probe speed, two runs agree, replay to the recorded hash, state round trip)
- Limits: a still `--export-image` does not run scripts, so it shows the entity poses stored in the yaml (the demo stores the first frame's pose); use `--export-sequence` to render the flight. Replay from a mid-run recording start of a scene with any rigid body diverges (the physics module saves only joints and rebuilds the PhysX actors, so a restored run is not bit-identical); the path itself is not involved

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
