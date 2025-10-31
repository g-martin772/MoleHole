#include "ThreadManager.h"
#include <spdlog/spdlog.h>
#include "Simulation/Scene.h"

namespace Threading {

ThreadManager::ThreadManager() {
    spdlog::debug("ThreadManager constructor");
}

ThreadManager::~ThreadManager() {
    shutdown();
}

void ThreadManager::initialize() {
    initialize(nullptr);
}

void ThreadManager::initialize(GLFWwindow* mainContext) {
    if (m_initialized.load(std::memory_order_acquire)) {
        spdlog::warn("ThreadManager already initialized");
        return;
    }
    
    spdlog::info("Initializing ThreadManager (Phase 3 - with ViewportRenderThread)");
    
    m_mainContext = mainContext;
    
    // Phase 1: Create the data structures
    m_sceneBuffer = std::make_unique<TripleBuffer<Scene>>();
    m_renderQueue = std::make_unique<ThreadSafeQueue<RenderCommand>>();
    m_simulationQueue = std::make_unique<ThreadSafeQueue<SimulationCommand>>();
    m_loadQueue = std::make_unique<ThreadSafeQueue<LoadRequest>>();
    
    // Phase 2: Initialize resource loader
    m_resourceLoader.initialize(nullptr);  // For now, no shared context
    
    // Phase 3: Initialize viewport render thread
    if (mainContext) {
        m_viewportRenderThread.initialize(mainContext);
        m_viewportRenderThread.start();
        spdlog::info("Viewport render thread started");
    } else {
        spdlog::warn("No main context provided - viewport render thread not started");
    }
    
    m_initialized.store(true, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    
    spdlog::info("ThreadManager initialized successfully");
}

void ThreadManager::shutdown() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }
    
    spdlog::info("Shutting down ThreadManager");
    
    m_running.store(false, std::memory_order_release);
    
    // Phase 3: Stop viewport render thread
    m_viewportRenderThread.stop();
    
    // Phase 2: Shutdown resource loader
    m_resourceLoader.shutdown();
    
    // Shutdown queues to wake any waiting threads
    if (m_renderQueue) {
        m_renderQueue->shutdown();
    }
    if (m_simulationQueue) {
        m_simulationQueue->shutdown();
    }
    if (m_loadQueue) {
        m_loadQueue->shutdown();
    }
    
    // Join threads if they exist (Phase 4 will implement simulation thread)
    if (m_simulationThread && m_simulationThread->joinable()) {
        m_simulationThread->join();
    }
    if (m_resourceLoaderThread && m_resourceLoaderThread->joinable()) {
        m_resourceLoaderThread->join();
    }
    
    m_initialized.store(false, std::memory_order_release);
    
    spdlog::info("ThreadManager shutdown complete");
}

// Placeholder implementations for Phase 1
// These will be fully implemented in later phases

void ThreadManager::startSimulation() {
    // Phase 4: Start simulation thread
    spdlog::debug("ThreadManager::startSimulation() - not yet implemented");
}

void ThreadManager::stopSimulation() {
    // Phase 4: Stop simulation thread
    spdlog::debug("ThreadManager::stopSimulation() - not yet implemented");
}

void ThreadManager::pauseSimulation() {
    // Phase 4: Pause simulation thread
    spdlog::debug("ThreadManager::pauseSimulation() - not yet implemented");
}

void ThreadManager::dispatchRenderCommand(RenderCommand cmd) {
    // Phase 3: Dispatch render command to viewport thread
    m_viewportRenderThread.dispatchCommand(std::move(cmd));
}

void ThreadManager::dispatchSimulationCommand(SimulationCommand cmd) {
    // Phase 4: Dispatch simulation command
    if (m_simulationQueue) {
        m_simulationQueue->push(std::move(cmd));
    }
}

void ThreadManager::dispatchLoadRequest(LoadRequest req) {
    // Phase 2: Dispatch load request to resource loader thread
    if (m_loadQueue) {
        m_loadQueue->push(std::move(req));
    }
}

Scene* ThreadManager::getUIScene() const {
    // Phase 4: Return UI-accessible scene from triple buffer
    if (m_sceneBuffer) {
        return const_cast<Scene*>(m_sceneBuffer->getUIBuffer());
    }
    return nullptr;
}

ThreadMetrics ThreadManager::getThreadMetrics(const std::string& name) const {
    // Phase 5: Return actual metrics
    ThreadMetrics metrics;
    metrics.threadName = name;
    return metrics;
}

// Thread functions - to be implemented in Phase 4

void ThreadManager::simulationThreadFunc() {
    // Phase 4: Simulation thread implementation
    spdlog::info("Simulation thread started");
    
    while (m_running.load(std::memory_order_acquire)) {
        // Wait for simulation commands
        auto cmd = m_simulationQueue->pop();
        if (!cmd.has_value()) {
            break; // Shutdown signal
        }
        
        // TODO: Simulation implementation
    }
    
    spdlog::info("Simulation thread stopped");
}

void ThreadManager::resourceLoaderThreadFunc() {
    // Phase 2: Resource loader thread implementation
    spdlog::info("Resource loader thread started");
    
    while (m_running.load(std::memory_order_acquire)) {
        // Wait for load requests
        auto req = m_loadQueue->pop();
        if (!req.has_value()) {
            break; // Shutdown signal
        }
        
        // TODO: Resource loading implementation
    }
    
    spdlog::info("Resource loader thread stopped");
}

} // namespace Threading
