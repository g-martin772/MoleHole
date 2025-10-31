# Multi-Threading Implementation Plan for MoleHole

## Table of Contents
1. [Overview](#overview)
2. [Objectives](#objectives)
3. [Current Architecture Analysis](#current-architecture-analysis)
4. [Thread Architecture Design](#thread-architecture-design)
5. [Thread Responsibilities](#thread-responsibilities)
6. [Synchronization Strategies](#synchronization-strategies)
7. [Implementation Phases](#implementation-phases)
8. [OpenGL Context Management](#opengl-context-management)
9. [Data Structures and Communication](#data-structures-and-communication)
10. [Potential Pitfalls and Solutions](#potential-pitfalls-and-solutions)
11. [Testing Strategy](#testing-strategy)
12. [Performance Monitoring](#performance-monitoring)
13. [Future Enhancements](#future-enhancements)

---

## Overview

This document outlines a comprehensive plan for implementing multi-threading in the MoleHole black hole simulation application. The goal is to improve responsiveness, enable parallel processing of simulation and rendering tasks, and provide a smooth user experience even under heavy computational loads.

**Current State:** Single-threaded application where UI, simulation, and rendering all execute sequentially on the main thread.

**Target State:** Multi-threaded architecture with dedicated threads for UI operations, viewport rendering, physics simulation, and resource loading.

---

## Objectives

### Primary Goals
1. **Snappy UI Responsiveness**: Maintain UI refresh at 60+ Hz regardless of viewport rendering or simulation load
2. **Background Resource Loading**: Load meshes, textures, and scenes asynchronously without blocking the UI
3. **Parallel Simulation**: Run physics calculations independently from rendering
4. **Smooth Rendering**: Decouple viewport frame rate from UI frame rate for optimal visual quality

### Success Metrics
- UI remains responsive (< 16ms frame time) even during heavy simulation
- Viewport can render at target frame rate (30-60 FPS) independently
- Resource loading doesn't block user interaction
- No visual artifacts or race conditions
- Minimal latency between simulation state and rendered output (< 1 frame)

---

## Current Architecture Analysis

### Existing Structure

The MoleHole application follows a three-layer architecture:

```
Application Layer (src/Application/)
├── Application (Singleton)
│   ├── Window Management (GLFW)
│   ├── UI (ImGui) 
│   ├── State Persistence (AppState)
│   └── Animation System (AnimationGraph)
│
Renderer Layer (src/Renderer/)
├── Renderer
│   ├── BlackHoleRenderer (Compute Shader Raytracing)
│   ├── VisualRenderer
│   ├── Camera & Input
│   └── Mesh Loading (GLTFMesh)
│
Simulation Layer (src/Simulation/)
└── Simulation
    ├── Scene Management
    ├── Physics State (BlackHoles, Objects)
    └── Animation Execution (GraphExecutor)
```

### Current Execution Flow

**Main Loop (Application::Run):**
```cpp
while (!ShouldClose()) {
    // 1. Poll Events
    HandleWindowEvents();
    
    // 2. Update (Sequential)
    m_simulation.Update(deltaTime);      // Physics simulation
    m_ui.Update(deltaTime);              // UI logic, state saving
    m_exportRenderer.Update();           // Video export if active
    
    // 3. Render (Sequential)
    m_renderer.BeginFrame();
    m_renderer.HandleMousePicking(scene);
    m_ui.RenderDockspace(scene);         // ImGui docking
    m_ui.RenderMainUI(GetFPS(), scene);  // ImGui panels
    m_renderer.RenderScene(scene);       // Viewport rendering
    m_ui.RenderSimulationControls();     // ImGui overlays
    m_renderer.EndFrame();               // Swap buffers
}
```

### Threading Challenges Identified

1. **OpenGL Context Binding**: OpenGL contexts are thread-local and cannot be shared trivially
2. **Scene Data Races**: Scene accessed by simulation (write) and renderer (read) simultaneously
3. **ImGui Thread Safety**: ImGui is not thread-safe and must run on main thread
4. **GLFW Event Polling**: Must occur on main thread (GLFW requirement)
5. **Resource Loading**: Currently blocks main thread during mesh/texture loading
6. **Animation Graph Execution**: Modifies scene state during simulation updates

---

## Thread Architecture Design

### Proposed Four-Thread Model

```
┌─────────────────────────────────────────────────────────────┐
│                    MAIN / UI THREAD                         │
│  - GLFW Event Polling (Required)                           │
│  - ImGui Rendering @ 60+ Hz                                │
│  - Command Dispatch to other threads                       │
│  - State Saving (AppState)                                 │
└─────────┬────────────────────────────────────────┬─────────┘
          │                                        │
          │ Commands                               │ Status
          ▼                                        ▼
┌─────────────────────────┐          ┌──────────────────────────┐
│  VIEWPORT RENDER THREAD │          │   SIMULATION THREAD      │
│  - OpenGL Context #2    │          │   - Physics Updates      │
│  - Compute Shader Exec  │          │   - Graph Execution      │
│  - Raytracing           │◄─────────┤   - Object Transforms    │
│  - Scene Rendering      │  Scene   │   - Collision Detection  │
│  - Target: 30-60 FPS    │   Copy   │   - Variable Time Step   │
└─────────────────────────┘          └──────────────────────────┘
          │                                        │
          │ Mesh Load Request                      │ Scene State
          ▼                                        │
┌─────────────────────────┐                       │
│  RESOURCE LOADER THREAD │                       │
│  - Async Mesh Loading   │                       │
│  - Texture Loading      │◄──────────────────────┘
│  - Scene File I/O       │
│  - Shared Context #3    │
└─────────────────────────┘
```

### Thread Interaction Diagram

```
Time ──────────────────────────────────────────►

MAIN     ┌UI┐ ┌UI┐ ┌UI┐ ┌UI┐ ┌UI┐ ┌UI┐ ┌UI┐
         │  │ │  │ │  │ │  │ │  │ │  │ │  │  60 Hz
         └──┘ └──┘ └──┘ └──┘ └──┘ └──┘ └──┘

VIEWPORT     ┌───Render───┐   ┌───Render───┐
             │            │   │            │  30-60 Hz
             └────────────┘   └────────────┘

SIM          ┌──Update─┐ ┌──Update─┐ ┌──Update─┐
             │         │ │         │ │         │  Variable
             └─────────┘ └─────────┘ └─────────┘

RESOURCE                ┌────Load Mesh────┐
                        │                 │
                        └─────────────────┘
```

---

## Thread Responsibilities

### Main / UI Thread

**Responsibilities:**
- **GLFW Event Polling** (`glfwPollEvents()`) - MUST be on main thread
- **ImGui Frame Lifecycle**:
  - `ImGui::NewFrame()`, `ImGui_ImplGlfw_NewFrame()`, `ImGui_ImplOpenGL3_NewFrame()`
  - Render all ImGui windows and panels
  - `ImGui::Render()`, `ImGui_ImplOpenGL3_RenderDrawData()`
- **Command Queue Management**: Dispatch commands to worker threads
- **State Persistence**: Save `AppState` every 5 seconds
- **Scene Management**: Handle scene load/save/new from UI
- **FPS Monitoring**: Display performance metrics

**OpenGL Context:** Primary context (Context #1) for ImGui rendering

**Key Pattern:**
```cpp
void MainThread::Run() {
    while (!shouldClose) {
        // Event polling (GLFW requirement)
        glfwPollEvents();
        
        // Process input
        processInput();
        
        // ImGui frame start
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        // Render all UI panels
        renderUI();
        
        // Dispatch commands to other threads
        dispatchRenderCommand();
        dispatchSimulationCommand();
        
        // ImGui frame end
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        
        // Swap buffers for UI
        glfwSwapBuffers(window);
    }
}
```

**Thread Safety Concerns:**
- All ImGui calls must be on this thread
- Scene access must be read-only (read from triple buffer)
- No blocking waits for other threads

---

### Viewport Render Thread

**Responsibilities:**
- **Viewport Rendering**: Execute all viewport rendering independent of UI
- **Compute Shader Execution**: Black hole raytracing via compute shaders
- **Camera Updates**: Apply camera movements and calculate view matrices
- **Scene Rendering**:
  - Black hole rendering (Schwarzschild & Kerr metrics)
  - Mesh rendering (GLTF objects)
  - Sphere rendering (planets)
  - Debug visualizations (gravity grid, ray paths)
- **FBO Management**: Render to framebuffer textures consumed by UI thread
- **Frame Rate Independence**: Target 30-60 FPS regardless of UI refresh rate

**OpenGL Context:** Separate shared context (Context #2)

**Key Pattern:**
```cpp
void ViewportRenderThread::Run() {
    // Create shared context
    glfwMakeContextCurrent(sharedContext);
    
    while (!shouldStop) {
        // Wait for render command from main thread
        RenderCommand cmd = commandQueue.pop();
        
        // Copy scene state from triple buffer
        Scene* renderScene = sceneBuffer.getReadBuffer();
        
        // Update camera
        updateCamera(cmd.deltaTime, renderScene->camera);
        
        // Render to FBO
        glBindFramebuffer(GL_FRAMEBUFFER, viewportFBO);
        
        switch (cmd.viewportMode) {
            case ViewportMode::Simulation3D:
                render3DSimulation(renderScene);
                break;
            case ViewportMode::SimulationVisual:
                renderVisualSimulation(renderScene);
                break;
            // ... other modes
        }
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        // Signal completion
        renderCompleteFlag.store(true);
    }
}
```

**Thread Safety Concerns:**
- Scene must be read-only copy from triple buffer
- FBO must be synchronized with main thread's texture binding
- No direct GLFW calls except context-related

---

### Simulation Thread

**Responsibilities:**
- **Physics Simulation**: Update black hole positions, velocities
- **Animation Graph Execution**:
  - Execute Start events (once on simulation start)
  - Execute Tick events (every frame during simulation)
  - Update scene objects via setter nodes
- **Object Transforms**: Apply keyframe animations, procedural motion
- **Collision Detection**: If implemented in future
- **Time Management**: Handle simulation time stepping (can be variable rate)

**OpenGL Context:** None (pure computation)

**Key Pattern:**
```cpp
void SimulationThread::Run() {
    while (!shouldStop) {
        // Wait for simulation command
        SimulationCommand cmd = commandQueue.pop();
        
        if (cmd.state != Simulation::State::Running) {
            continue;
        }
        
        // Get writable scene from triple buffer
        Scene* simScene = sceneBuffer.getWriteBuffer();
        
        // Execute animation graph
        if (cmd.executeStartEvent && !startEventExecuted) {
            graphExecutor->ExecuteStartEvent(simScene);
            startEventExecuted = true;
        }
        
        if (cmd.executeTickEvent) {
            graphExecutor->ExecuteTickEvent(simScene, cmd.deltaTime);
        }
        
        // Update physics
        updatePhysics(simScene, cmd.deltaTime);
        
        // Commit scene changes
        sceneBuffer.commitWriteBuffer();
        
        // Signal completion
        simulationCompleteFlag.store(true);
    }
}
```

**Thread Safety Concerns:**
- Scene must be exclusive write access via triple buffer
- Animation graph must be thread-safe or locked during execution
- No OpenGL calls allowed

---

### Resource Loader Thread

**Responsibilities:**
- **Async Mesh Loading**: Load GLTF models in background
- **Async Texture Loading**: Load textures (skybox, planet textures)
- **Scene File I/O**: Load/save scene YAML files
- **Mesh Caching**: Populate renderer's `m_meshCache` asynchronously
- **Priority Queue**: Handle high-priority loads (e.g., user-requested scene)

**OpenGL Context:** Shared context (Context #3) for texture/buffer uploads

**Key Pattern:**
```cpp
void ResourceLoaderThread::Run() {
    // Create shared context for GL resource uploads
    glfwMakeContextCurrent(resourceContext);
    
    while (!shouldStop) {
        // Wait for load request
        LoadRequest request = requestQueue.pop();
        
        switch (request.type) {
            case LoadRequest::Type::Mesh: {
                // Load mesh from disk
                auto mesh = loadGLTFMesh(request.path);
                
                // Upload to GPU (requires GL context)
                mesh->UploadToGPU();
                
                // Add to cache (thread-safe)
                {
                    std::lock_guard lock(meshCacheMutex);
                    meshCache[request.path] = mesh;
                }
                
                // Notify completion
                request.callback(mesh);
                break;
            }
            case LoadRequest::Type::Scene: {
                // Load scene from YAML
                Scene scene;
                scene.Deserialize(request.path);
                
                // Notify completion
                request.callback(scene);
                break;
            }
            // ... other resource types
        }
    }
}
```

**Thread Safety Concerns:**
- Shared GL context must be properly synchronized
- Mesh cache access requires mutex
- File I/O can block, use priority queue

---

## Synchronization Strategies

### Triple Buffering for Scene State

**Concept:** Eliminate locks on the critical path by using three scene buffers:
- **Write Buffer**: Simulation thread exclusive access
- **Read Buffer**: Render thread exclusive access  
- **Swap Buffer**: Middle buffer for atomic swapping

**Implementation:**
```cpp
class TripleBuffer {
private:
    std::array<Scene, 3> buffers;
    std::atomic<int> readIndex{0};
    std::atomic<int> writeIndex{1};
    std::atomic<int> swapIndex{2};
    
public:
    Scene* getWriteBuffer() {
        return &buffers[writeIndex.load(std::memory_order_relaxed)];
    }
    
    Scene* getReadBuffer() {
        return &buffers[readIndex.load(std::memory_order_acquire)];
    }
    
    void commitWriteBuffer() {
        // Atomic swap: write buffer becomes new swap buffer
        int oldSwap = swapIndex.exchange(
            writeIndex.load(std::memory_order_relaxed),
            std::memory_order_release
        );
        writeIndex.store(oldSwap, std::memory_order_relaxed);
    }
    
    void swapReadBuffer() {
        // Atomic swap: swap buffer becomes new read buffer
        int oldRead = readIndex.exchange(
            swapIndex.load(std::memory_order_acquire),
            std::memory_order_relaxed
        );
        swapIndex.store(oldRead, std::memory_order_release);
    }
};
```

**Advantages:**
- Lock-free on critical path
- No blocking between simulation and rendering
- Maximum concurrency

**Disadvantages:**
- High memory usage (3x scene data)
- Scene copy cost on commit

**Scene Copy Performance:**
Scene objects are relatively lightweight:
- `BlackHole`: 60 bytes × typical count (1-10) = 600 bytes
- `MeshObject`: 80 bytes × typical count (0-20) = 1600 bytes
- `Sphere`: 72 bytes × typical count (0-10) = 720 bytes
- **Total:** ~3KB per scene copy (negligible)

---

### Command Queues

**Purpose:** Communicate commands from main thread to worker threads without blocking.

**Implementation:**
```cpp
template<typename T>
class LockFreeQueue {
private:
    std::queue<T> queue;
    std::mutex mutex;
    std::condition_variable cv;
    
public:
    void push(T&& item) {
        std::lock_guard lock(mutex);
        queue.push(std::forward<T>(item));
        cv.notify_one();
    }
    
    T pop() {
        std::unique_lock lock(mutex);
        cv.wait(lock, [this] { return !queue.empty(); });
        T item = std::move(queue.front());
        queue.pop();
        return item;
    }
    
    bool tryPop(T& item) {
        std::lock_guard lock(mutex);
        if (queue.empty()) return false;
        item = std::move(queue.front());
        queue.pop();
        return true;
    }
};
```

**Command Types:**

```cpp
// Main → Viewport Render Thread
struct RenderCommand {
    ViewportMode mode;
    float deltaTime;
    Camera cameraState;
    bool debugMode;
};

// Main → Simulation Thread
struct SimulationCommand {
    enum class Action { Start, Stop, Pause, Step };
    Action action;
    float deltaTime;
    bool executeStartEvent;
    bool executeTickEvent;
};

// Main → Resource Loader Thread
struct LoadRequest {
    enum class Type { Mesh, Texture, Scene };
    Type type;
    std::string path;
    std::function<void(void*)> callback;
    int priority; // Higher = more urgent
};
```

---

### Mesh Cache Synchronization

**Problem:** Multiple threads may access `Renderer::m_meshCache` concurrently.

**Solution:** Shared mutex (readers-writer lock)

```cpp
class ThreadSafeMeshCache {
private:
    std::unordered_map<std::string, std::shared_ptr<GLTFMesh>> cache;
    mutable std::shared_mutex mutex;
    
public:
    std::shared_ptr<GLTFMesh> get(const std::string& path) {
        std::shared_lock lock(mutex); // Multiple readers
        auto it = cache.find(path);
        return (it != cache.end()) ? it->second : nullptr;
    }
    
    void insert(const std::string& path, std::shared_ptr<GLTFMesh> mesh) {
        std::unique_lock lock(mutex); // Exclusive writer
        cache[path] = mesh;
    }
    
    bool contains(const std::string& path) const {
        std::shared_lock lock(mutex);
        return cache.find(path) != cache.end();
    }
};
```

---

### Animation Graph Synchronization

**Problem:** AnimationGraph accessed by main thread (editing) and simulation thread (execution).

**Solution:** Copy-on-execute pattern

```cpp
class ThreadSafeAnimationGraph {
private:
    std::unique_ptr<AnimationGraph> currentGraph;
    std::unique_ptr<AnimationGraph> executionGraph;
    std::atomic<bool> needsUpdate{false};
    std::mutex updateMutex;
    
public:
    void updateGraph(std::unique_ptr<AnimationGraph> newGraph) {
        std::lock_guard lock(updateMutex);
        currentGraph = std::move(newGraph);
        needsUpdate.store(true, std::memory_order_release);
    }
    
    AnimationGraph* getExecutionGraph() {
        if (needsUpdate.load(std::memory_order_acquire)) {
            std::lock_guard lock(updateMutex);
            if (needsUpdate.load(std::memory_order_relaxed)) {
                executionGraph = std::make_unique<AnimationGraph>(*currentGraph);
                needsUpdate.store(false, std::memory_order_release);
            }
        }
        return executionGraph.get();
    }
};
```

---

## Implementation Phases

### Phase 1: Infrastructure (Week 1-2)

**Goal:** Set up threading infrastructure without changing application logic.

**Tasks:**
1. Create thread management classes:
   - `ThreadManager`: Lifecycle management for all threads
   - `ThreadSafeQueue<T>`: Command queue implementation
   - `TripleBuffer<Scene>`: Scene synchronization
2. Implement atomic flags for thread state:
   - `std::atomic<bool> shouldStop`
   - `std::atomic<bool> renderComplete`
   - `std::atomic<bool> simulationComplete`
3. Add logging for thread events (spdlog with thread ID)
4. Create unit tests for synchronization primitives

**Files to Create:**
- `src/Threading/ThreadManager.h/cpp`
- `src/Threading/ThreadSafeQueue.h`
- `src/Threading/TripleBuffer.h`
- `src/Threading/CommandTypes.h`

**Validation:**
- Unit tests pass for queue operations
- Triple buffer stress test (concurrent read/write)
- No crashes or deadlocks in simple scenarios

---

### Phase 2: Resource Loader Thread (Week 3)

**Goal:** Move mesh/texture loading off main thread.

**Tasks:**
1. Create `ResourceLoaderThread` class
2. Set up shared OpenGL context for GPU uploads
3. Implement priority queue for load requests
4. Modify `Renderer::GetOrLoadMesh()` to dispatch async load
5. Add callback mechanism for load completion
6. Update UI to show "Loading..." states

**Code Changes:**
```cpp
// Before (blocking)
std::shared_ptr<GLTFMesh> Renderer::GetOrLoadMesh(const std::string& path) {
    if (auto it = m_meshCache.find(path); it != m_meshCache.end()) {
        return it->second;
    }
    auto mesh = std::make_shared<GLTFMesh>(path); // BLOCKS
    m_meshCache[path] = mesh;
    return mesh;
}

// After (async)
std::shared_ptr<GLTFMesh> Renderer::GetOrLoadMesh(const std::string& path) {
    if (auto cached = meshCache.get(path)) {
        return cached;
    }
    
    // Return placeholder, request async load
    LoadRequest request{
        .type = LoadRequest::Type::Mesh,
        .path = path,
        .callback = [this, path](void* data) {
            auto mesh = static_cast<GLTFMesh*>(data);
            meshCache.insert(path, std::shared_ptr<GLTFMesh>(mesh));
        }
    };
    resourceLoaderQueue.push(std::move(request));
    
    return placeholderMesh; // Simple fallback mesh
}
```

**Validation:**
- Scene loading doesn't freeze UI
- Large mesh loads happen in background
- Placeholder meshes render until load completes
- No GL errors from context sharing

---

### Phase 3: Viewport Render Thread (Week 4-5)

**Goal:** Decouple viewport rendering from UI refresh rate.

**Tasks:**
1. Create `ViewportRenderThread` class
2. Set up shared OpenGL context for rendering
3. Implement FBO rendering for viewport
4. Modify `Renderer::RenderScene()` to run on render thread
5. Add command queue for render commands
6. Implement texture sharing with main thread
7. Update camera input handling (via commands)

**Synchronization Pattern:**
```cpp
// Main Thread
void Application::Render() {
    m_renderer.BeginFrame(); // ImGui setup
    
    // Get latest rendered viewport texture
    GLuint viewportTexture = viewportRenderer.getCompletedTexture();
    
    // Render UI with viewport texture
    m_ui.RenderDockspace(scene);
    ImGui::Image((void*)(intptr_t)viewportTexture, viewportSize);
    m_ui.RenderMainUI(GetFPS(), scene);
    
    // Dispatch render command for next frame
    RenderCommand cmd{
        .mode = m_renderer.GetSelectedViewport(),
        .deltaTime = m_deltaTime,
        .cameraState = *m_renderer.camera,
        .debugMode = m_state.GetDebugMode()
    };
    viewportRenderQueue.push(cmd);
    
    m_renderer.EndFrame(); // Swap buffers
}

// Viewport Render Thread
void ViewportRenderThread::Run() {
    glfwMakeContextCurrent(sharedContext);
    
    while (!shouldStop) {
        RenderCommand cmd = commandQueue.pop();
        
        // Get read-only scene
        Scene* scene = sceneBuffer.getReadBuffer();
        
        // Render to FBO
        glBindFramebuffer(GL_FRAMEBUFFER, viewportFBOs[currentFBO]);
        renderScene(scene, cmd);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        // Mark texture as complete
        completedTextureIndex.store(currentFBO);
        currentFBO = (currentFBO + 1) % 2; // Double buffer FBOs
    }
}
```

**Validation:**
- UI maintains 60 FPS even when viewport renders at 30 FPS
- No tearing or artifacts in viewport
- Camera controls remain responsive
- Viewport mode switching works correctly

---

### Phase 4: Simulation Thread (Week 6)

**Goal:** Run physics and animation updates independently.

**Tasks:**
1. Create `SimulationThread` class
2. Implement scene triple buffering
3. Move `Simulation::Update()` logic to simulation thread
4. Modify `GraphExecutor` to run on simulation thread
5. Add command queue for simulation control
6. Handle simulation start/stop/pause from UI

**Scene Update Flow:**
```
┌──────────────┐
│  Main Thread │
│              │
│ UI Changes   │ (User edits scene)
│      ↓       │
│  Command     │ → SimulationCommand (Start/Stop/Pause)
│   Queue      │
└──────────────┘
       ↓
┌──────────────────┐
│ Simulation Thread│
│                  │
│ Get Write Buffer │ ← Triple Buffer
│      ↓           │
│ Execute Graph    │ (Animation nodes)
│      ↓           │
│ Update Physics   │ (Positions, velocities)
│      ↓           │
│ Commit Write     │ → Triple Buffer
└──────────────────┘
       ↓
┌──────────────────┐
│ Render Thread    │
│                  │
│ Swap Read Buffer │ ← Triple Buffer
│      ↓           │
│ Render Scene     │
└──────────────────┘
```

**Validation:**
- Simulation runs at stable rate independent of UI
- Scene changes propagate correctly (< 1 frame latency)
- Animation graph execution works on simulation thread
- No race conditions on scene access

---

### Phase 5: Integration and Optimization (Week 7-8)

**Goal:** Polish, optimize, and handle edge cases.

**Tasks:**
1. Add frame time metrics for each thread
2. Implement adaptive time stepping for simulation
3. Add thread affinity hints (pin to specific cores)
4. Optimize scene copy performance (only copy changed data)
5. Add configurable thread priorities
6. Handle window resize gracefully across threads
7. Implement proper shutdown sequence
8. Add thread state visualization in UI

**Performance Dashboard UI:**
```
┌─────────────────────────────────────┐
│ Thread Performance                  │
├─────────────────────────────────────┤
│ Main/UI:      16.2 ms (61 FPS)  ████│
│ Viewport:     28.5 ms (35 FPS)  ████│
│ Simulation:   12.1 ms (82 FPS)  ████│
│ Loader:       Idle              ░░░░│
├─────────────────────────────────────┤
│ Latency:      18 ms (1.1 frames)    │
│ Frame Drops:  0 (last minute)       │
└─────────────────────────────────────┘
```

**Validation:**
- All threads perform within target metrics
- No memory leaks over extended run (valgrind)
- Stress test: 100+ objects with complex animation
- Graceful degradation under heavy load

---

## OpenGL Context Management

### Context Creation Strategy

**Primary Context (Main Thread):**
```cpp
// Created during Application::Initialize()
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

GLFWwindow* mainWindow = glfwCreateWindow(width, height, "MoleHole", nullptr, nullptr);
glfwMakeContextCurrent(mainWindow);

if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    throw std::runtime_error("Failed to initialize GLAD");
}
```

**Shared Context (Viewport Render Thread):**
```cpp
// Created on render thread startup
glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Invisible window
GLFWwindow* sharedWindow = glfwCreateWindow(1, 1, "", nullptr, mainWindow); // Share with main

// Make current on render thread
glfwMakeContextCurrent(sharedWindow);

// Re-initialize GLAD on this thread
gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
```

**Shared Context (Resource Loader Thread):**
```cpp
// Similar to viewport, share resources with main context
GLFWwindow* resourceWindow = glfwCreateWindow(1, 1, "", nullptr, mainWindow);
```

### Context Switching Rules

1. **Each thread maintains its own current context** - No context switching between threads
2. **Main thread:** Always has main window context current
3. **Viewport render thread:** Always has shared context current
4. **Resource loader thread:** Always has resource context current
5. **Simulation thread:** No OpenGL context (pure computation)

### Shared Resources

**What CAN be shared between contexts:**
- Textures (glGenTextures, glBindTexture)
- Buffers (VBO, EBO, UBO)
- Shaders and Programs
- Vertex Arrays (with caveats)

**What CANNOT be shared:**
- Framebuffers (each context needs own FBOs)
- Default framebuffer (window-specific)
- Context state (glEnable, glDisable, etc.)

### Synchronization Between Contexts

**GPU-side synchronization:**
```cpp
// Render thread: Finish rendering to shared texture
glFlush();
GLsync fenceSync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

// Main thread: Wait for GPU to complete
GLenum result = glClientWaitSync(fenceSync, GL_SYNC_FLUSH_COMMANDS_BIT, timeout);
if (result == GL_TIMEOUT_EXPIRED) {
    spdlog::warn("GPU sync timeout");
}
glDeleteSync(fenceSync);
```

---

## Data Structures and Communication

### Scene Data Layout

**Current Scene Structure (src/Simulation/Scene.h):**
```cpp
struct Scene {
    std::string name;
    std::vector<BlackHole> blackHoles;
    std::vector<MeshObject> meshes;
    std::vector<Sphere> spheres;
    std::filesystem::path currentPath;
    Camera* camera;
    std::optional<SelectedObject> selectedObject;
    // ... methods
};
```

**Memory Layout Analysis:**
- **Total size:** ~5KB for typical scene (10 black holes, 20 meshes, 10 spheres)
- **Copy cost:** ~2 microseconds on modern CPU
- **Cache-friendly:** All vectors are contiguous in memory

**Optimization: Dirty Flag Pattern**
```cpp
struct Scene {
    // ... existing fields
    
    std::atomic<uint64_t> version{0}; // Increment on any change
    uint64_t lastRenderedVersion{0};  // Track what renderer saw
    
    void markDirty() {
        version.fetch_add(1, std::memory_order_release);
    }
    
    bool isDirty() const {
        return version.load(std::memory_order_acquire) != lastRenderedVersion;
    }
};
```

### Animation Graph Thread Safety

**Problem:** `AnimationGraph` modified in UI editor, executed in simulation thread.

**Solution:** Version-based copy
```cpp
class AnimationGraphManager {
private:
    std::unique_ptr<AnimationGraph> editorGraph;  // Main thread only
    std::unique_ptr<AnimationGraph> runtimeGraph; // Simulation thread only
    std::atomic<uint64_t> editorVersion{0};
    uint64_t runtimeVersion{0};
    std::mutex copyMutex;
    
public:
    // Called by UI thread when graph is edited
    void updateEditorGraph(AnimationGraph&& newGraph) {
        std::lock_guard lock(copyMutex);
        editorGraph = std::make_unique<AnimationGraph>(std::move(newGraph));
        editorVersion.fetch_add(1);
    }
    
    // Called by simulation thread before execution
    AnimationGraph* getRuntimeGraph() {
        uint64_t currentEditorVersion = editorVersion.load();
        if (currentEditorVersion != runtimeVersion) {
            std::lock_guard lock(copyMutex);
            runtimeGraph = std::make_unique<AnimationGraph>(*editorGraph);
            runtimeVersion = currentEditorVersion;
        }
        return runtimeGraph.get();
    }
};
```

### Camera State Communication

**Problem:** Camera updated by input (main thread) but used for rendering (render thread).

**Solution:** Command-embedded camera state
```cpp
struct CameraState {
    glm::vec3 position;
    glm::vec3 forward;
    glm::vec3 up;
    float fov;
    float nearPlane;
    float farPlane;
    
    CameraState() = default;
    CameraState(const Camera& camera) {
        position = camera.position;
        forward = camera.getForward();
        up = camera.getUp();
        fov = camera.fov;
        nearPlane = camera.nearPlane;
        farPlane = camera.farPlane;
    }
};

struct RenderCommand {
    CameraState cameraState; // Snapshot at command dispatch time
    // ... other fields
};
```

---

## Potential Pitfalls and Solutions

### 1. Scene Copy Performance Bottleneck

**Problem:** Triple buffering requires full scene copy on every simulation update.

**Mitigation Strategies:**
1. **Optimize Scene::operator=()** - Use move semantics for vectors
2. **Dirty flag optimization** - Only copy if simulation actually changed scene
3. **Delta encoding** - Only copy changed objects (complex, not recommended for Phase 1)

**Performance Target:**
- Scene copy: < 10 microseconds (current typical scene)
- If exceeded: Consider per-object dirty flags

### 2. OpenGL Context Switching Overhead

**Problem:** Context switching has cost (~100 microseconds).

**Solution:** Avoid context switching entirely by keeping each thread's context always current.

**Code Pattern (CORRECT):**
```cpp
void RenderThread::Run() {
    glfwMakeContextCurrent(sharedContext); // Once at startup
    
    while (!shouldStop) {
        // All GL calls here use shared context
        renderFrame();
    }
    
    // Context destroyed on thread exit
}
```

**Anti-pattern (INCORRECT):**
```cpp
void RenderThread::RenderFrame() {
    glfwMakeContextCurrent(sharedContext); // ❌ DON'T do this every frame
    renderFrame();
}
```

### 3. ImGui Thread Safety Violations

**Problem:** ImGui is NOT thread-safe. All ImGui calls must be on main thread.

**Solution:** Never call ImGui functions from worker threads. Pass display data via thread-safe structures.

**Example:**
```cpp
// ❌ WRONG: ImGui call from render thread
void RenderThread::Render() {
    ImGui::Text("FPS: %f", fps); // CRASH!
}

// ✅ CORRECT: Pass data to main thread
void RenderThread::Render() {
    metricsData.fps = calculateFPS();
}

void MainThread::RenderUI() {
    auto metrics = metricsData.load();
    ImGui::Text("FPS: %f", metrics.fps);
}
```

### 4. GLFW Main Thread Requirement

**Problem:** GLFW requires `glfwPollEvents()` on the main thread.

**Solution:** Always call `glfwPollEvents()` from main thread. Worker threads can create windows but not poll events.

**Validation:**
```cpp
// Ensure main thread calls this
void Application::Run() {
    while (!ShouldClose()) {
        glfwPollEvents(); // ✅ Main thread only
        // ... rest of loop
    }
}
```

### 5. Race Condition on Scene Selection

**Problem:** User selects object (main thread) while simulation is updating it (simulation thread).

**Solution:** Selection changes are considered UI-only state until next simulation frame.

**Pattern:**
```cpp
// Main thread: User clicks object
void UI::HandleObjectSelection(size_t objectIndex) {
    pendingSelection = objectIndex; // UI state only
}

// Simulation thread: Apply pending selection
void SimulationThread::Update() {
    if (pendingSelection.has_value()) {
        Scene* scene = sceneBuffer.getWriteBuffer();
        scene->SelectObject(pendingSelection.value());
        pendingSelection.reset();
    }
    // ... rest of update
}
```

### 6. Resource Loader Texture Uploads

**Problem:** Texture upload to GPU must happen on thread with GL context.

**Solution:** Resource loader thread has shared context for GPU uploads.

**Code:**
```cpp
void ResourceLoaderThread::LoadTexture(const std::string& path) {
    // CPU-side load (no GL context needed)
    int width, height, channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 0);
    
    // GPU upload (requires GL context)
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    
    stbi_image_free(data);
    
    // Store in thread-safe cache
    textureCache.insert(path, texture);
}
```

### 7. Animation Graph Execution During Edit

**Problem:** User editing graph while simulation executes it.

**Solution:** Use versioned copy pattern (see Data Structures section).

**Key Insight:** Copying the graph is cheap (< 1ms) compared to simulation frequency (60 Hz), so copy-on-modify is acceptable.

### 8. Frame Rate Mismatch Artifacts

**Problem:** Simulation at 60 Hz, viewport at 30 Hz - temporal aliasing.

**Solution:** Interpolation (optional, Phase 5):
```cpp
struct SceneSnapshot {
    Scene data;
    double timestamp;
};

Scene interpolateScenes(const SceneSnapshot& prev, const SceneSnapshot& next, double renderTime) {
    double t = (renderTime - prev.timestamp) / (next.timestamp - prev.timestamp);
    t = std::clamp(t, 0.0, 1.0);
    
    Scene result = prev.data;
    for (size_t i = 0; i < result.blackHoles.size(); i++) {
        result.blackHoles[i].position = glm::mix(
            prev.data.blackHoles[i].position,
            next.data.blackHoles[i].position,
            static_cast<float>(t)
        );
    }
    return result;
}
```

### 9. Shutdown Deadlock

**Problem:** Threads waiting on queues during shutdown.

**Solution:** Poison pill pattern:
```cpp
class ThreadSafeQueue {
private:
    std::atomic<bool> accepting{true};
    
public:
    void shutdown() {
        accepting.store(false);
        cv.notify_all(); // Wake all waiting threads
    }
    
    std::optional<T> pop() {
        std::unique_lock lock(mutex);
        cv.wait(lock, [this] { return !queue.empty() || !accepting; });
        
        if (!accepting && queue.empty()) {
            return std::nullopt; // Signal thread to exit
        }
        
        T item = std::move(queue.front());
        queue.pop();
        return item;
    }
};
```

### 10. Memory Ordering Issues

**Problem:** Weak memory ordering on ARM/non-x86 architectures.

**Solution:** Use appropriate `std::memory_order` for atomics:
```cpp
// Release-acquire ordering for synchronization points
version.store(newVersion, std::memory_order_release);  // Producer
uint64_t v = version.load(std::memory_order_acquire);  // Consumer

// Relaxed ordering for simple counters
frameCounter.fetch_add(1, std::memory_order_relaxed);
```

---

## Testing Strategy

### Unit Tests

**Threading Primitives:**
```cpp
TEST(ThreadSafeQueue, ConcurrentPushPop) {
    ThreadSafeQueue<int> queue;
    std::atomic<int> sum{0};
    
    // 10 producer threads
    std::vector<std::thread> producers;
    for (int i = 0; i < 10; i++) {
        producers.emplace_back([&queue, i] {
            for (int j = 0; j < 1000; j++) {
                queue.push(i * 1000 + j);
            }
        });
    }
    
    // 5 consumer threads
    std::vector<std::thread> consumers;
    for (int i = 0; i < 5; i++) {
        consumers.emplace_back([&queue, &sum] {
            for (int j = 0; j < 2000; j++) {
                sum.fetch_add(queue.pop());
            }
        });
    }
    
    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
    
    // Verify all elements were processed exactly once
    int expectedSum = (10000 - 1) * 10000 / 2; // Sum of 0..9999
    EXPECT_EQ(sum.load(), expectedSum);
}

TEST(TripleBuffer, NoDataRace) {
    TripleBuffer<Scene> buffer;
    std::atomic<bool> stop{false};
    
    // Writer thread
    std::thread writer([&] {
        int frame = 0;
        while (!stop) {
            Scene* scene = buffer.getWriteBuffer();
            scene->name = "Frame " + std::to_string(frame++);
            buffer.commitWriteBuffer();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    
    // Reader thread
    std::thread reader([&] {
        std::set<std::string> seenNames;
        while (!stop) {
            buffer.swapReadBuffer();
            Scene* scene = buffer.getReadBuffer();
            seenNames.insert(scene->name);
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    });
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop = true;
    writer.join();
    reader.join();
    
    // No crashes = success
}
```

### Integration Tests

**End-to-End Scenarios:**
1. **Simulation Loop Test**:
   - Start simulation
   - Verify scene updates propagate to renderer
   - Verify UI remains responsive
   - Stop simulation
   - Verify clean shutdown

2. **Resource Loading Test**:
   - Queue 10 mesh loads
   - Verify UI doesn't block
   - Verify all meshes loaded successfully
   - Verify mesh cache populated

3. **Stress Test**:
   - 100 black holes
   - Complex animation graph
   - Continuous camera movement
   - Run for 10 minutes
   - Check for memory leaks (valgrind)
   - Check for deadlocks

### Thread Sanitizer

**Usage:**
```bash
# Build with thread sanitizer
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" \
      ..
cmake --build .

# Run tests
./MoleHole --run-tests

# Run full application
./MoleHole
```

**What to look for:**
- Data races
- Lock ordering violations
- Deadlocks

### Performance Benchmarks

**Metrics to Track:**
```cpp
struct ThreadMetrics {
    std::string threadName;
    std::chrono::microseconds avgFrameTime;
    std::chrono::microseconds maxFrameTime;
    std::chrono::microseconds p95FrameTime; // 95th percentile
    size_t framesProcessed;
    size_t frameDrops;
};
```

**Target Metrics:**
- Main thread: < 16ms (60 FPS)
- Viewport render: < 33ms (30 FPS)
- Simulation: < 16ms (60 FPS)
- Resource loader: Varies (background priority)

**Benchmark Tool:**
```cpp
class PerformanceMonitor {
public:
    void beginFrame(const std::string& threadName) {
        frameStarts[threadName] = std::chrono::high_resolution_clock::now();
    }
    
    void endFrame(const std::string& threadName) {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end - frameStarts[threadName]
        );
        frameTimes[threadName].push_back(duration);
    }
    
    ThreadMetrics getMetrics(const std::string& threadName) {
        auto& times = frameTimes[threadName];
        std::sort(times.begin(), times.end());
        
        return ThreadMetrics{
            .threadName = threadName,
            .avgFrameTime = calculateAverage(times),
            .maxFrameTime = times.back(),
            .p95FrameTime = times[times.size() * 95 / 100],
            .framesProcessed = times.size()
        };
    }
};
```

---

## Performance Monitoring

### Real-Time Metrics Display

**UI Panel:**
```cpp
void UI::RenderThreadMetrics() {
    ImGui::Begin("Thread Performance");
    
    auto& monitor = Application::Instance().GetPerformanceMonitor();
    
    // Main thread
    auto mainMetrics = monitor.getMetrics("Main");
    ImGui::Text("Main/UI Thread");
    ImGui::Text("  Frame Time: %.2f ms", mainMetrics.avgFrameTime.count() / 1000.0);
    ImGui::Text("  FPS: %.1f", 1000000.0 / mainMetrics.avgFrameTime.count());
    ImGui::ProgressBar(
        mainMetrics.avgFrameTime.count() / 16666.0, // Target 60 FPS
        ImVec2(-1, 0)
    );
    
    // Viewport render thread
    auto renderMetrics = monitor.getMetrics("Viewport");
    ImGui::Text("Viewport Render Thread");
    ImGui::Text("  Frame Time: %.2f ms", renderMetrics.avgFrameTime.count() / 1000.0);
    ImGui::Text("  FPS: %.1f", 1000000.0 / renderMetrics.avgFrameTime.count());
    
    // Simulation thread
    auto simMetrics = monitor.getMetrics("Simulation");
    ImGui::Text("Simulation Thread");
    ImGui::Text("  Update Time: %.2f ms", simMetrics.avgFrameTime.count() / 1000.0);
    ImGui::Text("  Updates/sec: %.1f", 1000000.0 / simMetrics.avgFrameTime.count());
    
    // Latency metrics
    ImGui::Separator();
    ImGui::Text("Sim→Render Latency: %.2f ms", calculateSimToRenderLatency());
    
    ImGui::End();
}
```

### Logging Best Practices

**Thread-aware logging:**
```cpp
// Include thread ID in log pattern
spdlog::set_pattern("[%H:%M:%S.%e] [%t] [%^%l%$] %v");
//                              ^^^^ Thread ID

// Log thread lifecycle events
spdlog::info("Viewport render thread starting");
spdlog::info("Simulation thread stopped");

// Log performance warnings
if (frameTime > targetFrameTime * 1.5) {
    spdlog::warn("{} thread frame time exceeded target: {} ms (target: {} ms)",
                 threadName, frameTime, targetFrameTime);
}
```

### Profiling Integration

**Tracy Profiler Integration (Optional):**
```cpp
#ifdef ENABLE_PROFILING
#include <tracy/Tracy.hpp>

void RenderThread::Render() {
    ZoneScoped; // Tracy profiling zone
    ZoneName("Viewport Render", 15);
    
    {
        ZoneScopedN("Scene Copy");
        Scene* scene = sceneBuffer.getReadBuffer();
    }
    
    {
        ZoneScopedN("Black Hole Raytracing");
        blackHoleRenderer->Render(scene->blackHoles, camera, time);
    }
    
    FrameMark; // End of frame
}
#endif
```

---

## Future Enhancements

### Phase 6+: Advanced Features

**1. GPU-side Physics Simulation**
- Move black hole physics to compute shader
- Reduce CPU→GPU data transfer
- Enable real-time simulation of 1000+ black holes

**2. Async Scene Serialization**
- Save/load scene files on resource loader thread
- Show progress bar for large scenes
- Prevent UI freeze during save operations

**3. Thread Pool for Simulation**
- Replace single simulation thread with thread pool
- Parallelize per-object updates
- Scale to high object counts

**4. Lock-Free Data Structures**
- Replace mutexes with lock-free algorithms where possible
- Reduce contention on mesh cache
- Use `std::atomic` for command queues

**5. Adaptive Frame Rate**
- Dynamically adjust viewport render rate based on complexity
- Maintain 60 FPS for simple scenes, drop to 30 for complex
- Use frame time predictor

**6. Network Rendering**
- Distribute rendering across multiple machines
- Send render commands over network
- Aggregate results in main application

**7. Replay System**
- Record simulation state every frame
- Enable scrubbing through simulation timeline
- Requires efficient state compression

---

## Appendix: Code Examples

### Complete Thread Manager Implementation

```cpp
// ThreadManager.h
#pragma once
#include <thread>
#include <atomic>
#include <memory>
#include "ThreadSafeQueue.h"
#include "TripleBuffer.h"

class ThreadManager {
public:
    ThreadManager();
    ~ThreadManager();
    
    void initialize();
    void shutdown();
    
    // Thread control
    void startSimulation();
    void stopSimulation();
    void pauseSimulation();
    
    // Command dispatch
    void dispatchRenderCommand(RenderCommand cmd);
    void dispatchSimulationCommand(SimulationCommand cmd);
    void dispatchLoadRequest(LoadRequest req);
    
    // Scene access
    Scene* getUIScene() const;
    
    // Metrics
    ThreadMetrics getThreadMetrics(const std::string& name) const;
    
private:
    // Threads
    std::unique_ptr<std::thread> m_viewportRenderThread;
    std::unique_ptr<std::thread> m_simulationThread;
    std::unique_ptr<std::thread> m_resourceLoaderThread;
    
    // Synchronization
    std::atomic<bool> m_running{false};
    std::unique_ptr<TripleBuffer<Scene>> m_sceneBuffer;
    
    // Command queues
    ThreadSafeQueue<RenderCommand> m_renderQueue;
    ThreadSafeQueue<SimulationCommand> m_simulationQueue;
    ThreadSafeQueue<LoadRequest> m_loadQueue;
    
    // Thread functions
    void viewportRenderThreadFunc();
    void simulationThreadFunc();
    void resourceLoaderThreadFunc();
    
    // OpenGL contexts
    GLFWwindow* m_viewportContext = nullptr;
    GLFWwindow* m_resourceContext = nullptr;
};
```

### Complete Triple Buffer Implementation

```cpp
// TripleBuffer.h
#pragma once
#include <array>
#include <atomic>
#include <memory>

template<typename T>
class TripleBuffer {
public:
    TripleBuffer() {
        // Initialize all three buffers
        for (int i = 0; i < 3; i++) {
            m_buffers[i] = std::make_unique<T>();
        }
    }
    
    // Get buffer for writing (simulation thread)
    T* getWriteBuffer() {
        int index = m_writeIndex.load(std::memory_order_relaxed);
        return m_buffers[index].get();
    }
    
    // Commit written buffer (simulation thread)
    void commitWriteBuffer() {
        int currentWrite = m_writeIndex.load(std::memory_order_relaxed);
        int currentSwap = m_swapIndex.exchange(currentWrite, std::memory_order_release);
        m_writeIndex.store(currentSwap, std::memory_order_relaxed);
    }
    
    // Get buffer for reading (render thread)
    T* getReadBuffer() {
        int index = m_readIndex.load(std::memory_order_acquire);
        return m_buffers[index].get();
    }
    
    // Swap to latest committed buffer (render thread)
    void swapReadBuffer() {
        int currentRead = m_readIndex.load(std::memory_order_relaxed);
        int currentSwap = m_swapIndex.exchange(currentRead, std::memory_order_acquire);
        m_readIndex.store(currentSwap, std::memory_order_relaxed);
    }
    
    // Get buffer for UI (main thread) - reads from current read buffer
    const T* getUIBuffer() const {
        int index = m_readIndex.load(std::memory_order_acquire);
        return m_buffers[index].get();
    }
    
private:
    std::array<std::unique_ptr<T>, 3> m_buffers;
    std::atomic<int> m_readIndex{0};
    std::atomic<int> m_writeIndex{1};
    std::atomic<int> m_swapIndex{2};
};
```

---

## Conclusion

This multi-threading implementation plan provides a comprehensive roadmap for transforming MoleHole from a single-threaded application into a highly responsive, parallel-processing simulation. The phased approach minimizes risk while delivering incremental improvements at each stage.

**Key Takeaways:**
1. **Four-thread architecture** provides optimal separation of concerns
2. **Triple buffering** eliminates locks on the critical rendering path
3. **Command queues** enable decoupled communication between threads
4. **Phase-by-phase implementation** reduces complexity and risk
5. **OpenGL context management** requires careful design but is well-understood
6. **Testing strategy** ensures correctness and performance

**Estimated Timeline:** 8 weeks for full implementation with testing

**Next Steps:**
1. Review this plan with team
2. Set up development branch for threading work
3. Begin Phase 1: Infrastructure implementation
4. Regular code reviews and performance testing

---

**Document Version:** 1.0  
**Last Updated:** 2025-10-31  
**Author:** GitHub Copilot  
**Status:** Ready for Implementation
