#pragma once
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <chrono>
#include "ThreadSafeQueue.h"
#include "TripleBuffer.h"
#include "CommandTypes.h"
#include "ResourceLoader.h"
#include "ViewportRenderThread.h"
#include "SimulationThread.h"

// Forward declarations
struct GLFWwindow;
class Scene;
class AnimationGraph;

namespace Threading {

/**
 * Performance metrics for a thread
 */
struct ThreadMetrics {
    std::string threadName;
    std::chrono::microseconds avgFrameTime{0};
    std::chrono::microseconds maxFrameTime{0};
    std::chrono::microseconds p95FrameTime{0};
    size_t framesProcessed{0};
    size_t frameDrops{0};
};

/**
 * Manages all worker threads and their communication
 * Currently a placeholder for Phase 1 - will be fully implemented in later phases
 */
class ThreadManager {
public:
    ThreadManager();
    ~ThreadManager();
    
    // Lifecycle
    void initialize();
    void initialize(GLFWwindow* mainContext); // Phase 3: With main context
    void shutdown();
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }
    
    // Thread control (Phase 4)
    void startSimulation();
    void stopSimulation();
    void pauseSimulation();
    void resumeSimulation();
    
    // Animation graph control (Phase 4)
    void setAnimationGraph(AnimationGraph* graph);
    
    // Command dispatch (to be implemented in Phase 2-4)
    void dispatchRenderCommand(RenderCommand cmd);
    void dispatchSimulationCommand(SimulationCommand cmd);
    void dispatchLoadRequest(LoadRequest req);
    
    // Scene access (Phase 4)
    Scene* getUIScene() const;
    TripleBuffer<Scene>* getSceneBuffer() const { return m_sceneBuffer.get(); }
    
    // Metrics (to be implemented in Phase 5)
    ThreadMetrics getThreadMetrics(const std::string& name) const;
    
    // Resource loader access (Phase 2)
    ResourceLoader& getResourceLoader() { return m_resourceLoader; }
    const ResourceLoader& getResourceLoader() const { return m_resourceLoader; }
    
    // Viewport render thread access (Phase 3)
    ViewportRenderThread& getViewportRenderThread() { return m_viewportRenderThread; }
    const ViewportRenderThread& getViewportRenderThread() const { return m_viewportRenderThread; }
    
    // Simulation thread access (Phase 4)
    SimulationThread& getSimulationThread() { return m_simulationThread; }
    const SimulationThread& getSimulationThread() const { return m_simulationThread; }
    
    // Prevent copying
    ThreadManager(const ThreadManager&) = delete;
    ThreadManager& operator=(const ThreadManager&) = delete;
    
private:
    // State
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_initialized{false};
    
    // Threads (Phase 4 - using SimulationThread class now)
    std::unique_ptr<std::thread> m_resourceLoaderThread;
    
    // Synchronization (Phase 4 - triple buffer for scene)
    std::unique_ptr<TripleBuffer<Scene>> m_sceneBuffer;
    
    // Command queues (legacy - may be removed in future)
    std::unique_ptr<ThreadSafeQueue<RenderCommand>> m_renderQueue;
    std::unique_ptr<ThreadSafeQueue<SimulationCommand>> m_simulationQueue;
    std::unique_ptr<ThreadSafeQueue<LoadRequest>> m_loadQueue;
    
    // OpenGL contexts (Phase 3)
    GLFWwindow* m_viewportContext = nullptr;
    GLFWwindow* m_resourceContext = nullptr;
    GLFWwindow* m_mainContext = nullptr; // Reference to main window context
    
    // Resource loader (Phase 2)
    ResourceLoader m_resourceLoader;
    
    // Viewport render thread (Phase 3)
    ViewportRenderThread m_viewportRenderThread;
    
    // Simulation thread (Phase 4)
    SimulationThread m_simulationThread;
    
    // Thread functions (Phase 4)
    void resourceLoaderThreadFunc();
};

} // namespace Threading
