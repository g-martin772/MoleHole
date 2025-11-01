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
    
    spdlog::info("Initializing ThreadManager (Phase 4 - with SimulationThread)");
    
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
    
    // Phase 4: Initialize simulation thread
    m_simulationThread.initialize(m_sceneBuffer.get());
    m_simulationThread.start();
    spdlog::info("Simulation thread started");
    
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
    
    // Phase 4: Stop simulation thread
    m_simulationThread.stop();
    
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
    
    // Join threads if they exist
    if (m_resourceLoaderThread && m_resourceLoaderThread->joinable()) {
        m_resourceLoaderThread->join();
    }
    
    m_initialized.store(false, std::memory_order_release);
    
    spdlog::info("ThreadManager shutdown complete");
}

// Thread control implementations (Phase 4)

void ThreadManager::startSimulation() {
    SimulationCommand cmd(SimulationCommand::Action::Start, 0.0f, true, false);
    m_simulationThread.dispatchCommand(std::move(cmd));
}

void ThreadManager::stopSimulation() {
    SimulationCommand cmd(SimulationCommand::Action::Stop);
    m_simulationThread.dispatchCommand(std::move(cmd));
}

void ThreadManager::pauseSimulation() {
    SimulationCommand cmd(SimulationCommand::Action::Pause);
    m_simulationThread.dispatchCommand(std::move(cmd));
}

void ThreadManager::resumeSimulation() {
    SimulationCommand cmd(SimulationCommand::Action::Continue);
    m_simulationThread.dispatchCommand(std::move(cmd));
}

void ThreadManager::setAnimationGraph(AnimationGraph* graph) {
    m_simulationThread.setAnimationGraph(graph);
}

void ThreadManager::dispatchRenderCommand(RenderCommand cmd) {
    // Phase 3: Dispatch render command to viewport thread
    m_viewportRenderThread.dispatchCommand(std::move(cmd));
}

void ThreadManager::dispatchSimulationCommand(SimulationCommand cmd) {
    // Phase 4: Dispatch simulation command to simulation thread
    m_simulationThread.dispatchCommand(std::move(cmd));
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

// Thread functions - Phase 4 uses SimulationThread class

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
