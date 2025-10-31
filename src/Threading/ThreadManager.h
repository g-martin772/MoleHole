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

// Forward declarations
struct GLFWwindow;
class Scene;

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
    
    // Thread control (to be implemented in Phase 4)
    void startSimulation();
    void stopSimulation();
    void pauseSimulation();
    
    // Command dispatch (to be implemented in Phase 2-4)
    void dispatchRenderCommand(RenderCommand cmd);
    void dispatchSimulationCommand(SimulationCommand cmd);
    void dispatchLoadRequest(LoadRequest req);
    
    // Scene access (to be implemented in Phase 4)
    Scene* getUIScene() const;
    
    // Metrics (to be implemented in Phase 5)
    ThreadMetrics getThreadMetrics(const std::string& name) const;
    
    // Resource loader access (Phase 2)
    ResourceLoader& getResourceLoader() { return m_resourceLoader; }
    const ResourceLoader& getResourceLoader() const { return m_resourceLoader; }
    
    // Viewport render thread access (Phase 3)
    ViewportRenderThread& getViewportRenderThread() { return m_viewportRenderThread; }
    const ViewportRenderThread& getViewportRenderThread() const { return m_viewportRenderThread; }
    
    // Prevent copying
    ThreadManager(const ThreadManager&) = delete;
    ThreadManager& operator=(const ThreadManager&) = delete;
    
private:
    // State
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_initialized{false};
    
    // Threads (Phase 3 - using ViewportRenderThread class now)
    std::unique_ptr<std::thread> m_simulationThread;
    std::unique_ptr<std::thread> m_resourceLoaderThread;
    
    // Synchronization (to be implemented in Phase 2-4)
    std::unique_ptr<TripleBuffer<Scene>> m_sceneBuffer;
    
    // Command queues (to be implemented in Phase 2-4)
    std::unique_ptr<ThreadSafeQueue<RenderCommand>> m_renderQueue;
    std::unique_ptr<ThreadSafeQueue<SimulationCommand>> m_simulationQueue;
    std::unique_ptr<ThreadSafeQueue<LoadRequest>> m_loadQueue;
    
    // OpenGL contexts (to be implemented in Phase 3)
    GLFWwindow* m_viewportContext = nullptr;
    GLFWwindow* m_resourceContext = nullptr;
    GLFWwindow* m_mainContext = nullptr; // Reference to main window context
    
    // Resource loader (Phase 2)
    ResourceLoader m_resourceLoader;
    
    // Viewport render thread (Phase 3)
    ViewportRenderThread m_viewportRenderThread;
    
    // Thread functions (to be implemented in Phase 2-4)
    void simulationThreadFunc();
    void resourceLoaderThreadFunc();
};

} // namespace Threading
