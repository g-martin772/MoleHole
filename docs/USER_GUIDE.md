# MoleHole User Guide

## Windows
Open from sidebar or View menu.
| Window | Use |
|---|---|
| Scene | outliner, add objects, inspector |
| Camera | FOV, speed, sensitivity, reset, scene-camera options |
| System | tick rate, perf, system info |
| Debug | render toggles, accretion disk, overlays |
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

### Debug
- Toggle: black holes, spheres, lensing, redshift, Doppler, physically accurate
- Accretion disk: height, speed, noise scale/LOD, volumetric
- Ray step size, max steps, adaptive step rate
- Metric type
- Gravity grid, collider wireframe, object paths
- View menu → Camera Gizmos

### Config files
- `config.json`: window size, fullscreen, shader dirs, hot reload, fonts, script dirs, combo dirs
- Script dirs: `GPP.Assets.Scripts.Directories`
- Combo dirs: `GPP.Assets.Combos.Directories`

## Components
| Component | Fields |
|---|---|
| Transform | position, rotation, scale |
| BlackHole | mass, spin, charge, spin axis |
| Sphere | radius, spin, color, spin axis, texture |
| Mesh | mesh asset |
| Camera | FOV, near, far, primary |
| RigidBody / Collider / Velocity | physics |
| Scripts | list of script components + props |
- Add via inspector → Add Component
- Add Mesh: browse file, optional convex-hull collider

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
- GUI: Export → Export Render…
- Flags: `--width --height --duration --fps --scene --ray-step-size --max-ray-steps`
- Full table: `docs/export-instructions.md`

## Scene file
```yaml
Scene: Name
Entities:
  - Guid: 1
    Components:
      Metadata: {Guid: 1, Name: BH, TypeTag: BlackHole}
      Transform: {Position: [0,0,0], Rotation: [0,0,0,1], Scale: [1,1,1]}
      BlackHole: {Mass: 1, Spin: 0.4, Charge: 0, SpinAxis: [0,1,0]}
Graphs: ... 
```
- Templates in `templates/`
- 