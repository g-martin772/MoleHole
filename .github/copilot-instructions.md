# MoleHole - Black Hole Simulation Project

## Overview
MoleHole is a C++23 OpenGL 4.6 application simulating black hole physics using compute-shader-based raytracing. The architecture follows a three-layer design:
- **Application Layer** (`src/Application/`): Window management, UI (ImGui), state persistence, and node-based animation system
- **Renderer Layer** (`src/Renderer/`): OpenGL rendering, including specialized black hole raytracing, mesh loading, and camera controls
- **Simulation Layer** (`src/Simulation/`): Scene management, graph-based animation execution, and physics state

## Build System
- **CMake-based** with C++23 standard
- **Dependencies are git submodules** - always use `git submodule update --init --recursive` after clone
- Build task: `Build (cmake)` runs `cmake --build build --config Debug`
- Build output: `build/MoleHole` executable
- FFmpeg integration on Linux for video export

## Critical Architecture Patterns

### Singleton Application Pattern
The `Application` class is a singleton accessed via `Application::Instance()`. Key static accessors:
- `Application::State()` - Access application state (window, camera, settings)
- `Application::GetRenderer()` - Access renderer subsystem
- `Application::GetSimulation()` - Access simulation subsystem

### Scene and Object System
Scenes (`Scene` struct in `Simulation/Scene.h`) contain three object types:
- `std::vector<BlackHole>` - Physics objects with mass, position, spin (Kerr metric)
- `std::vector<MeshObject>` - GLTF meshes with transforms
- `std::vector<Sphere>` - Textured spheres with rotation

Objects are selected via `Scene::SelectedObject` with type-safe index access. Use `GetSelectedObjectPosition/Rotation/Scale()` for unified transform editing.

### State Persistence (AppState)
`AppState` class persists to `config.yaml` using yaml-cpp:
- Window state (size, position, vsync)
- Camera state (position, orientation)
- Rendering settings (FOV, ray steps, debug modes)
- Custom properties via `SetProperty<T>(key, value)` / `GetProperty<T>(key, default)`

State saves automatically every 5 seconds via `UI::Update()` timer.

### Node-Based Animation System
`AnimationGraph` (`Application/AnimationGraph.h`) uses ImGui Node Editor for visual scripting:
- **Node types**: Event (Start/Tick), Function (math ops), Variable, Decomposer (object property access), Setter, Control flow
- **Execution**: `GraphExecutor` interprets the graph during simulation updates
- **Pin system**: Typed connections (Flow, F1-F4, Vec2-4, BlackHole/Mesh/Sphere objects)
- **Scene binding**: Nodes can reference scene objects by index via `SceneObjectIndex`

Serialization in scene YAML files under `animation_graph:` section.

### Compute Shader Raytracing
Black hole rendering uses compute shaders (`shaders/blackhole_raytrace.comp`):
- **Schwarzschild & Kerr metrics**: Simulates gravitational lensing and frame-dragging
- **3D LUT**: Lookup table for Kerr distortion (`u_kerrLookupTable`)
- **Skybox environment mapping**: HDR skybox at `assets/space.hdr`
- **Debug modes**: Accessible via `DebugMode` enum - influence zones, deflection magnitude, gravity grids

Output to texture displayed via quad shader.

### Viewport Modes
`Renderer::ViewportMode` enum switches rendering modes:
- `Demo1` - Legacy demo
- `Rays2D` - 2D ray visualization
- `Simulation3D` - 3D physics simulation
- `SimulationVisual` - Visual rendering with post-processing

Each mode calls different render methods in `Renderer.cpp`.

## Key Workflows

### Adding New Scene Objects
1. Extend relevant vector in `Scene` struct (`blackHoles`, `meshes`, `spheres`)
2. Update `Scene::Serialize/Deserialize` for YAML persistence
3. Add UI controls in `UI::Render*Section()` methods
4. If animatable, add corresponding decomposer/setter nodes in `AnimationGraph`

### Shader Development
- Shaders in `shaders/` directory, loaded by `Shader` class
- Compute shaders use `layout(local_size_x=16, local_size_y=16)`
- Uniform management in `BlackHoleRenderer::UpdateUniforms()`
- Hot-reload not implemented - requires app restart

### Adding Animation Graph Nodes
1. Add `NodeSubType` enum value
2. Implement `CreateXNode()` factory in `AnimationGraph`
3. Add execution logic in `GraphExecutor::ExecuteNode()` switch
4. Update serialization in `AnimationGraph::Serialize/Deserialize`
5. Add UI rendering in `NodeBuilder::Render*()` methods

## File Patterns

### Header Organization
- Headers use `#pragma once`
- Forward declarations minimize includes
- GLM types use full namespace (`glm::vec3`, not `using`)
- GLFW forward declared as `struct GLFWwindow`

### Error Handling
- spdlog for all logging: `spdlog::info/warn/error/debug()`
- Try-catch blocks in public API boundaries (scene load/save)
- OpenGL errors not consistently checked - add `glGetError()` when debugging

### Resource Management
- RAII for OpenGL resources in renderer classes
- Meshes cached in `Renderer::m_meshCache` unordered_map
- Shaders owned by unique_ptr, initialized in `Init()` methods

## Dependencies (in `dependencies/`)
- **GLFW 3** - Windowing (X11 on Linux)
- **GLAD** - OpenGL loader (4.6 core profile)
- **GLM** - Math library
- **ImGui** - UI (docking branch)
- **ImGuizmo** - 3D transform gizmos
- **ImGui Node Editor** - Animation graph (`dependencies/nodes`)
- **tinygltf** - GLTF mesh loading
- **yaml-cpp** - Config and scene serialization
- **spdlog** - Logging
- **stb_image** - Image loading (header-only in `dependencies/stb-image`)
- **nativefiledialog-extended** - File dialogs (requires GTK on Linux)

## Common Pitfalls
- **Submodule initialization**: Most build failures come from missing submodules
- **OpenGL context**: Renderer must be initialized before any GL calls
- **Scene lifetime**: Scene owned by Simulation, use pointers not references
- **State saving**: AppState changes auto-save, but explicit `SaveState()` needed before crash-sensitive operations
- **Animation graph execution order**: Start event runs once, Tick event each frame during simulation
- **YAML deserialization**: Scene files expect specific structure - check `templates/scene.yaml`
