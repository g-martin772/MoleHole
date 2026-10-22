# MoleHole User Guide

## Windows
Open from sidebar or View menu.
| Window | Use |
|---|---|
| Scene | outliner, add objects, inspector |
| Camera | FOV, speed, sensitivity, reset, scene-camera options |
| System | tick rate, perf, system info |
| Log | console |
| Render | render profiles, every render setting, stats, GPU capabilities |
| Debug | display units, CPU precision, overlays, debug view |
| Viewport HUD | stats overlay |
| General Relativity | theory + metric info |
| Science | science info |
| Animation Graph | graph editor |
| Settings | UI scale, font, intro |

## Basics
- File → New / Open / Save / Save As / templates / recent
- Scene = single `.yaml`
- Play / Pause / Stop: bar over viewport
- Stop restores the scene as before Play
- Edits while paused/stopped are undoable (ctrl+z/y)

## Keybinds
### Global
| Key | Action |
|---|---|
| Ctrl+N | new scene |
| Ctrl+O | open |
| Ctrl+S | save |
| Ctrl+Shift+S | save as |

### Viewport
| Key | Action |
|---|---|
| Hold RMB + move | look |
| W A S D | move |
| E / Q | up / down |
| F | focus selected object (auto distance) |
| Scroll | zoom in/out while focused |
| Alt + hold RMB + move | orbit focused object |
| 1 or T | translate gizmo |
| 2 or R | rotate gizmo |
| 3 or S | scale gizmo |
| Esc | deselect |
| LMB | pick object |

### Scene window
| Key | Action |
|---|---|
| Del | delete selected |
| F2 | rename |
| Ctrl+Z | undo |
| Ctrl+Shift+Z / Ctrl+Y | redo |
| Right-click | rename, duplicate, add child, unparent, delete |

### Animation Graph
| Key | Action |
|---|---|
| RMB drag | pan |
| Scroll | zoom |
| LMB drag | move nodes / box select |
| Del / Backspace | delete selection |
| Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y | undo / redo |
| Ctrl+C / X / V | copy / cut / paste |
| Ctrl+D | duplicate |
| Ctrl+A | select all |
| C | comment box around selection |
| F9 | toggle breakpoint (may be unmapped) |
| Right-click canvas | node palette (type to search) |
| Up / Down / Enter | palette navigation |

## Options
### Settings
- Play intro on startup
- UI scale
- Font + font size

### Camera
- FOV, speed, mouse sensitivity
- Possess scene camera while playing
- Preview scene camera in edit mode
- Reset camera

### System
- Sim tick rate (slider + presets)

### Render
- All render settings live here, grouped (Spacetime, Transport, Radiometry, Disk, Objects, Camera, Sampling, Post, Overlays, Debug); search box on top
- Each setting shows a cost badge (5 pips), a tooltip with its key, and a reset button when it differs from the profile's base
- See [Render profiles and settings](#render-profiles-and-settings)

### Debug
- Display units, CPU simulation precision
- Overlays (gravity grid, collider wireframe, object paths) and the debug view of the active profile
- View menu → Camera Gizmos

### Config files
- `config.json`: window size, fullscreen, shader dirs, hot reload, fonts, script dirs, combo dirs
- Script dirs: `GPP.Assets.Scripts.Directories`
- Combo dirs: `GPP.Assets.Combos.Directories`

## Units
- Everything is stored and computed in SI: metres, kilograms, seconds, kelvin. Scene files hold SI numbers only
- Numeric fields show the unit that fits the value
- Drag: logarithmic (1 %/px, Shift ×5, Alt/Ctrl ×0.1), passes through zero. Click: type `1.5 AU`, `3 Msun`, `10 km`,
  `2 Lj`, `4.3e6 Msun`, `1 Mearth`, `0.1 c`. A number without a unit is read in the unit currently shown
- Hover a field to see the SI value and a few other units
- Right-click a field to force one display unit for it
- Precision: CPU simulation f32/f64/f128, GPU render f32/f64

## Render profiles and settings
A **profile** is a named bundle of every render setting. A **context** says which profile is used when:

| Context | Used when | Default profile |
|---|---|---|
| Viewport: editing | sim stopped or paused | Flat |
| Viewport: playing | sim running | Lensed |
| Viewport: reference | "Render reference" is on | Reference |
| Export | exporting (GUI or CLI) | Lensed |

Built-in profiles (read-only):

| Profile | Ray transport | Use |
|---|---|---|
| Flat | straight rays, no lensing, no bloom | editing, placing objects |
| Lensed | analytic lens, bloom, flare | interactive default, what the app always looked like |
| Traced | integrated geodesics, step 0.05, 4000 steps | cinematic preview, seconds per frame |
| Reference | integrated geodesics, step 0.01, 100000 steps, volumetric disk, no lens flare | final renders, minutes per frame |

- Change a setting while a built-in is selected: it is forked into `<name> (edited)` and assigned to that context
- Duplicate / rename / delete user profiles in the Render window
- "Edit what the viewport shows" (on): the Render window edits the profile of the active context. Off: pick any context to prepare it (e.g. Export)
- Settings are stored in the scene (`Render:` key, only user profiles as differences from their base, plus the context map), so a scene renders the same on another machine. Scenes without it use the app defaults (`.gpp/molehole_state.yaml`, "Use as app default")
- A scene saved before profiles existed picks up your old toggles as a profile called `Legacy` (once)
- Render settings edited while playing survive Stop
- Render reference: viewport uses the Reference context's profile until you switch back
- Stats overlay: profile, fps, GPU ms and tracer resolution in the viewport corner, then one line per active render stage with its GPU ms (Trace, Bloom, Composite...)

```yaml
Render:
  Version: 1
  Contexts: {Edit: Flat, Play: Cinematic, Reference: Reference, Export: Cinematic}
  Profiles:
    - Name: Cinematic
      Base: Lensed
      Set: {disk.volumetric: true, post.bloom_intensity: 8}
```

### Settings worth knowing
- `transport.mode`: Straight / Analytic lens / Integrated. Only Integrated evaluates the spacetime (`spacetime.solution`: Schwarzschild, Kerr, Reissner-Nordstrom, Kerr-Newman; spin a* and charge q* come from each black hole)
- `camera.observer`: ZAMO (default) / Static / Orbiting / Raindrop (free fall). Sets how the camera moves relative to the black hole, Integrated transport only. A moving camera sees the sky aberrated (shifted towards its direction of motion) and the disk Doppler shifted; the sky background itself is not colour- or brightness-shifted. Where the observer cannot exist (Static inside the ergosphere, Orbiting inside the photon orbit) the camera falls back to the ZAMO. `camera.observer_orbit` (Prograde / Retrograde) picks the direction for Orbiting. Background: `docs/notes/observers.md`
- `sampling.resolution_scale` (25-200 %): the tracer renders at that share of the viewport and the image is scaled. Bloom width is kept the same on screen
- `sampling.governor` + `sampling.budget_ms`: while editing or playing, lowers the resolution scale (to 25 % at worst) when the frame time exceeds the budget and restores it with headroom; never in reference views or exports
- `debug.view`: step count heat map, termination reason (sky, horizon, object, opaque disk, ran out of steps, crosshair), geodesic constraint drift and ergosphere & ISCO (Integrated only). Bloom, flare, FXAA and tone mapping are off in debug views
- Disk physics: inner edge at the ISCO, Page-Thorne temperature from the hole's mass and `disk.accretion_rate` (Eddington ratio), Keplerian rotation of the active metric, redshift and beaming from the metric. `disk.volumetric` off = thin disk shaded where rays cross the plane, on = flared gas with radiative transfer (`disk.height` = scale height in M at r = 10 M). Background: `docs/notes/disk_rendering.md`
- Disk look: `disk.opacity` (translucent to opaque), `disk.hot_spots`, `disk.noise_scale` / `disk.noise_lod` (turbulence), `disk.speed` (multiplier of the simulation clock for the disk only: 1 = the physical rotation at the scene's Time Scale, 0 = frozen). `disk.display_temperature` (K) colours the hottest gas like an exposure for colour: all temperatures scale together, so ratios and shifts stay physical; 0 = physical colour (stellar-mass holes are ~10^7 K, blue-white)
- `post.tonemap` (None / Reinhard / ACES) with `post.exposure_mode` (Manual / Auto), `post.exposure` (EV) and `post.exposure_key`. Bloom and flare thresholds apply to the exposed image. None shows the raw tracer output like before HDR
- `transport.gpu_precision` f64 needs `shaderFloat64`; a setting the GPU cannot run is greyed with the reason, and loaded profiles fall back to the default (shown in the Render window)
- Mesh objects are rasterised over the traced image (not lensed, always in front). The Integrated transport does not draw spheres yet
- `overlay.characteristic_radii`: every black hole's horizon (grey), ergosphere (purple, in two meridian planes), equatorial photon orbit (orange), marginally bound orbit (green) and ISCO (blue). Bright = prograde, dim = retrograde With the Integrated transport the tracer draws the rings itself, lensed like the disk (the plain lines would sit inside the lensed shadow); object path trails and predicted orbits are traced too inside 50 M of the hole (lensed, with secondary images and Einstein-ring arcs) and are plain aberrated lines further out.
- `overlay.predicted_orbits`: each Geodesic Body's future path, integrated ahead from its current state. Bright now, dim later, stops at the horizon
- `overlay.prediction_span`: how far ahead to predict, as coordinate time in M = GM/c³ of the body's hole
- Like the other overlays, these are rasterised: not lensed or aberrated
- Disk brightness: exposure is anchored to the hottest gas as seen from far away (face-on), not to its rest-frame peak. Schwarzschild disks look as before; spinning disks are brighter (about 1.4× at a* = 0.5, 3× at 0.9, 20× at 0.998) and near-extremal disks look like Thorne-limit ones. Lower `post.exposure` for old high-spin scenes if needed. Background: `docs/notes/near_extremal.md`, `docs/notes/orbit_overlays_and_nodes.md`

### Environment
- `GPP_GPU=<name part>`: choose the GPU by name instead of the highest score
- `GPP_GPU_DISABLE=rayQuery,shaderFloat64,...`: behave like a GPU without those features (keys in the Render window's Device section)

## Components
| Component | Fields |
|---|---|
| Transform | position, rotation, scale |
| BlackHole | mass (kg), spin range, spin a*, spin gap, charge q*, spin axis; derived r_g, horizon, ISCO, effective a* |
| Sphere | radius (m), spin, color, spin axis, texture |
| Mesh | mesh asset, metres per model unit |
| Geodesic Body | launch mode, retrograde, launch velocity; readouts status, proper time, clock rate, speed vs ZAMO, r/M |
| Camera | FOV, near, far, primary |
| RigidBody / Collider / Velocity | physics |
| Scripts | list of script components + props |
- Add via inspector → Add Component
- Add Mesh: browse file, optional convex-hull collider

### Black hole spin range
- `Spin range` ThorneLimit (default): a* is capped at 0.998, the most disk accretion can spin a hole up to (Thorne 1974)
- `Spin range` NearExtremal: the `Spin a*` field is ignored and a* = 1 - `Spin gap`. The gap (1 - a*) runs from 1e-15 to 0.002, default 1.3e-14 (Interstellar's Gargantua, about one part in 100 trillion from maximal)
- The inspector shows `Effective a*` and `dt/dτ at the ISCO` (about 10.8 at a* = 0.998, about 62 000 at 1 - 1.3e-14: one hour there is about 7 years far away)
- The film itself rendered Gargantua with a = 0.6 for visual reasons. The renderer handles near-extremal holes, but the disk inside about 1.01 M is not drawn (rays are captured there)
- Background: `docs/notes/near_extremal.md`

### Geodesic Body
- A body in free fall along an exact geodesic of one black hole. `Launch` picks how it starts: Zamo, Static, Orbiting (with `Retrograde`), Raindrop or Velocity (`Velocity (r, θ, φ) / c`)
- Readouts: `Status`, `Proper time`, `Clock rate dτ/dt`, `Speed vs ZAMO`, `r/M`
- Simulation time is the distant observer's coordinate time, scaled by the scene TimeScale
- Moving the body in the editor relaunches it
- Background: `docs/notes/geodesic_bodies.md`

## Animation Graph
- One scene can hold many named graphs (tabs/dropdown)
- Event nodes start flows (white wires); data wires are typed colors
- Run: Play. Pick runtime in dropdown: **Luau** (default) or **Interpreter**
- Interpreter can't run Luau-only nodes (shown on node)

### Node categories
- Events: Start, Tick, Custom Event, On Key, On Trigger
- Flow Control: Branch, For Loop, Sequence, Do Once, Gate, Switch, While, For Each Entity
- Latent: Delay, Wait Until, Wait For Event, Interpolate (Luau only)
- Entities: Spawn, Clone, Destroy
- Relativity: Launch Circular Orbit, Clock Rate
- Objects: Get / Decompose / Set per component (auto-generated for every component)
- Math: add, sub, mul, div, min, max, lerp, clamp, sin, cos, sqrt, length, distance, look-at…
- Variables: typed, per graph
- Utility: Print
- Functions: reusable subgraphs
- Reroute + comment boxes

### Combos
Premade, prewired groups. Add menu → Combos, fill few fields.
- Move Object To
- Orbit Camera Around
- Spawn Prefab At
- Fade Property
- On Start Set Property
- Save selection as combo → your own library (`combos/*.yaml`)

### Relativity nodes
- Launch Circular Orbit: inputs Entity (with a Geodesic Body), Radius (M of its hole), Azimuth (rad about the spin axis), Retrograde. Puts the body on a circular equatorial orbit; the simulation follows it from the next tick. Warns when impossible (inside the photon orbit, no Geodesic Body)
- Clock Rate: inputs world Position and Observer (0 ZAMO, 1 Static, 2 Orbiting, 3 Raindrop). Outputs dτ/dt against a distant observer; 0 where that observer cannot exist
- Both behave identically in the interpreter and the Luau runtime
- A Geodesic Body's Proper time, Clock rate and Speed are readable with the regular Get / Decompose Geodesic Body nodes

### Debugging
- Executing wires highlight
- Live values on pins
- Breakpoints, execution trace
- Validation badges on nodes

## Scripting (Luau)
- Files in `scripts/*.luau`
- Attach: select entity → Scripts → Add Script
- Returns table: `properties`, `OnStart(self)`, `OnTick(self, dt)`
- `self.entity`, `self.props.X`; own fields on `self`
- Property types: `float`, `bool`, `entity`, vectors…
- Props show in inspector, saved in scene

```lua
return {
  properties = { { name = "Speed", type = "float", default = 1 } },
  OnTick = function(self, dt)
    local p = scene.get(self.entity, "Transform", "Position")
    scene.set(self.entity, "Transform", "Position", p + vector.create(self.props.Speed * dt, 0, 0))
  end,
}
```

### API
- `scene.exists(e)`, `scene.get(e, comp, field)`, `scene.set(e, comp, field, v)`
- `scene.find(name)`, `scene.name(e)`, `scene.query(...)`, `scene.look_at(...)`
- `render.set(key, value)`, `render.get(key)`: change or read a render setting (see `./build/MoleHole --list-settings`) for the running simulation only. The profile is not edited; it reverts on Stop or scene load. Unknown keys or bad values are script errors
- `vector.create(x,y,z)`, `vec2()`, `vec4()`
- `math.random` is seeded by the sim (deterministic)
- Sandboxed: no io / os

### Example
- `scripts/spring_arm_camera.luau` – orbit camera around target
- Demo scene: `templates/spring-arm-demo.yaml`
- Turn on "Possess scene camera" to look through it

## Exports
CLI, headless. Output PNG / MP4 (needs ffmpeg).
```sh
./build/MoleHole --export-image out.png --width 1920 --height 1080 --scene templates/test-scene.yaml
./build/MoleHole --export-video out.mp4 --duration 10 --fps 60
```
- GUI: Export → Export Render… (pick a render profile, default: the Export context's)
- Flags: `--width --height --duration --fps --scene --sim-start S --time-scale X --profile NAME --set key=value --list-settings`; the older `--ray-step-size --max-ray-steps --gpu-precision --physically-accurate --metric` still work
- Every export writes `<output>.render.yaml` (scene hash, profile and all settings, camera, git commit, GPU, GPU ms, wall time)
- The disk follows the simulation clock `t_sim` (physical seconds, shown as Sim time in the Simulation window, frozen while paused, restored on Stop), not the wall clock: exports are reproducible. `--sim-start S` sets the first frame's `t_sim`, `--time-scale X` the physical seconds per second of video (the scene's Time Scale); frame k of a video sits at `S + k X / fps`
- `tools/bench.py`: time every profile, keep or compare golden images
- Full table: `docs/export-instructions.md`

## Installing
```sh
cmake --preset release-vcpkg
cmake --build --preset release-vcpkg
cmake --install build-release          # prefix ~/.local, or --prefix DIR
```
| Where | What |
|---|---|
| `<prefix>/bin/molehole` | symlink to the binary |
| `<prefix>/lib/molehole/` | binary, libraries, UI plugins (hot reload is off when installed) |
| `<prefix>/share/molehole/` | default `config.json`, shaders, fonts, assets, templates, scripts, combos |
| `<prefix>/share/applications/molehole.desktop`, `share/icons/hicolor/scalable/apps/molehole.svg` | app menu entry |
| `~/.config/molehole/` | your `config.json` overrides, app state, UI layout |
| `~/.cache/molehole/` | shader cache, temp files |
| `~/Documents/MoleHole/{scenes,exports,scripts,combos,models}` | your files; scripts and combos here are found before the bundled ones |

- Paths follow `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME` and `XDG_DOCUMENTS_DIR` (`~/.config/user-dirs.dirs`)
- The install ends with `molehole --install-desktop`. It writes a window-rule snippet to `~/.config/molehole/wm/` for Hyprland, sway or i3. On a terminal it asks once before adding the rule to your WM config (with a backup); `--apply-wm-rules` / `--no-wm-rules` skip the question. `MOLEHOLE_SKIP_DESKTOP=1` skips the step; it is also skipped with `DESTDIR`
- Uninstall: `molehole --uninstall-desktop`, then delete `<prefix>/bin/molehole`, `<prefix>/lib/molehole`, `<prefix>/share/molehole` and, if wanted, `~/.config/molehole`, `~/.cache/molehole`, `~/Documents/MoleHole`
- System dependencies (not bundled): a Vulkan driver (ray query GPU), shaderc (`libshaderc_shared`), Wayland/X11 and the usual desktop libraries; optional ffmpeg (video export), latex + dvipng (inline LaTeX)

## Scene file
```yaml
Scene: Name
SchemaVersion: 2
TimeScale: 1
Entities:
  - Guid: 1
    Components:
      Metadata: {Guid: 1, Name: BH, TypeTag: BlackHole}
      Transform: {Position: [0,0,0], Rotation: [0,0,0,1], Scale: [1,1,1]}   # metres
      BlackHole: {Mass: 1.98841e+30, Spin: 0.4, Charge: 0, SpinAxis: [0,1,0]}   # kg, a*, q*
Graphs: ... 
```
- Templates in `templates/`
- 
- `templates/m1-orbits.yaml`: milestone M1 demo. Kerr hole (10^6 Msun, a* = 0.9), prograde and retrograde circular orbits at 10 M (frame dragging: different periods), an inclined eccentric orbit (periapsis and nodal precession), a raindrop, and a scene camera riding its own orbit with an animation graph that keeps the hole in view. Its "M1 Orbits" render profile turns on both orbit overlays
- `templates/gargantua-orbits.yaml`: near-extremal Gargantua with orbiting bodies and an orbiting camera
