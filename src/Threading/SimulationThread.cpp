#include "SimulationThread.h"
#include <spdlog/spdlog.h>
#include "Simulation/Scene.h"
#include "Simulation/GraphExecutor.h"
#include "Application/AnimationGraph.h"

namespace Threading {

SimulationThread::SimulationThread() {
    spdlog::debug("SimulationThread constructor");
}

SimulationThread::~SimulationThread() {
    stop();
}

void SimulationThread::initialize(TripleBuffer<Scene>* sceneBuffer) {
    if (m_initialized.load(std::memory_order_acquire)) {
        spdlog::warn("SimulationThread already initialized");
        return;
    }
    
    if (!sceneBuffer) {
        spdlog::error("SimulationThread: sceneBuffer is null");
        return;
    }
    
    spdlog::info("Initializing SimulationThread (Phase 4)");
    
    m_sceneBuffer = sceneBuffer;
    m_savedScene = std::make_unique<Scene>();
    
    m_initialized.store(true, std::memory_order_release);
    
    spdlog::info("SimulationThread initialized successfully");
}

void SimulationThread::start() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        spdlog::error("SimulationThread not initialized - call initialize() first");
        return;
    }
    
    if (m_running.load(std::memory_order_acquire)) {
        spdlog::warn("SimulationThread already running");
        return;
    }
    
    m_running.store(true, std::memory_order_release);
    m_thread = std::make_unique<std::thread>(&SimulationThread::simulationThreadFunc, this);
    
    spdlog::info("SimulationThread started");
}

void SimulationThread::stop() {
    if (!m_running.load(std::memory_order_acquire)) {
        return;
    }
    
    spdlog::info("Stopping SimulationThread");
    
    m_running.store(false, std::memory_order_release);
    m_commandQueue.shutdown();
    
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
    
    m_initialized.store(false, std::memory_order_release);
    
    spdlog::info("SimulationThread stopped");
}

void SimulationThread::dispatchCommand(SimulationCommand cmd) {
    if (m_running.load(std::memory_order_acquire)) {
        m_commandQueue.push(std::move(cmd));
    }
}

void SimulationThread::setAnimationGraph(AnimationGraph* graph) {
    m_animationGraph = graph;
}

void SimulationThread::simulationThreadFunc() {
    spdlog::info("Simulation thread started");
    
    // Main simulation loop
    while (m_running.load(std::memory_order_acquire)) {
        // Wait for simulation command
        auto cmd = m_commandQueue.pop();
        if (!cmd.has_value()) {
            break; // Shutdown signal
        }
        
        handleCommand(cmd.value());
    }
    
    spdlog::info("Simulation thread stopped");
}

void SimulationThread::handleCommand(const SimulationCommand& cmd) {
    switch (cmd.action) {
        case SimulationCommand::Action::Start: {
            if (!m_simulationRunning.load(std::memory_order_acquire)) {
                // Save initial scene state
                saveSceneState();
                
                m_simulationTime.store(0.0f, std::memory_order_release);
                m_startEventExecuted = false;
                
                // Execute start event if animation graph exists
                if (m_animationGraph) {
                    Scene* scene = m_sceneBuffer->getWriteBuffer();
                    m_graphExecutor = std::make_unique<GraphExecutor>(m_animationGraph, scene);
                    m_graphExecutor->ExecuteStartEvent();
                    m_startEventExecuted = true;
                    m_sceneBuffer->commitWriteBuffer();
                }
                
                m_simulationRunning.store(true, std::memory_order_release);
                spdlog::info("Simulation started on simulation thread");
            }
            break;
        }
        
        case SimulationCommand::Action::Stop: {
            if (m_simulationRunning.load(std::memory_order_acquire)) {
                // Restore original scene state
                restoreSceneState();
                
                m_simulationTime.store(0.0f, std::memory_order_release);
                m_simulationRunning.store(false, std::memory_order_release);
                m_startEventExecuted = false;
                m_graphExecutor.reset();
                
                spdlog::info("Simulation stopped and reset to initial state");
            }
            break;
        }
        
        case SimulationCommand::Action::Pause: {
            if (m_simulationRunning.load(std::memory_order_acquire)) {
                m_simulationRunning.store(false, std::memory_order_release);
                float time = m_simulationTime.load(std::memory_order_acquire);
                spdlog::info("Simulation paused at time: {:.2f}s", time);
            }
            break;
        }
        
        case SimulationCommand::Action::Continue: {
            if (!m_simulationRunning.load(std::memory_order_acquire)) {
                m_simulationRunning.store(true, std::memory_order_release);
                spdlog::info("Simulation resumed");
            }
            break;
        }
        
        case SimulationCommand::Action::Step: {
            // Single step simulation
            if (cmd.executeTickEvent && m_animationGraph && m_graphExecutor) {
                updateSimulation(cmd.deltaTime);
            }
            break;
        }
    }
    
    // If simulation is running, update it
    if (m_simulationRunning.load(std::memory_order_acquire)) {
        if (cmd.executeTickEvent && cmd.deltaTime > 0.0f) {
            updateSimulation(cmd.deltaTime);
        }
    }
}

void SimulationThread::updateSimulation(float deltaTime) {
    // Get write buffer from triple buffer
    Scene* scene = m_sceneBuffer->getWriteBuffer();
    
    // Execute animation graph tick event
    if (m_animationGraph && m_graphExecutor) {
        m_graphExecutor->ExecuteTickEvent(deltaTime);
    }
    
    // TODO: Add physics simulation here
    // For now, just update the simulation time
    
    // Update simulation time
    float currentTime = m_simulationTime.load(std::memory_order_acquire);
    m_simulationTime.store(currentTime + deltaTime, std::memory_order_release);
    
    // Commit changes to triple buffer
    m_sceneBuffer->commitWriteBuffer();
}

void SimulationThread::saveSceneState() {
    // Get read buffer to save current state
    const Scene* currentScene = m_sceneBuffer->getUIBuffer();
    *m_savedScene = *currentScene;
    spdlog::debug("Scene state saved on simulation thread");
}

void SimulationThread::restoreSceneState() {
    // Get write buffer to restore state
    Scene* scene = m_sceneBuffer->getWriteBuffer();
    *scene = *m_savedScene;
    m_sceneBuffer->commitWriteBuffer();
    spdlog::debug("Scene state restored on simulation thread");
}

} // namespace Threading
