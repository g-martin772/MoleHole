#pragma once
#include <thread>
#include <atomic>
#include <memory>
#include "CommandTypes.h"
#include "ThreadSafeQueue.h"
#include "TripleBuffer.h"

// Forward declarations
class Scene;
class AnimationGraph;
class GraphExecutor;

namespace Threading {

/**
 * Simulation thread - runs physics and animation updates independently
 * Runs at variable rate while UI and rendering maintain their own rates
 */
class SimulationThread {
public:
    SimulationThread();
    ~SimulationThread();
    
    // Initialize with triple buffer
    void initialize(TripleBuffer<Scene>* sceneBuffer);
    
    // Start the simulation thread
    void start();
    
    // Stop the simulation thread
    void stop();
    
    // Check if thread is running
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }
    
    // Dispatch simulation command
    void dispatchCommand(SimulationCommand cmd);
    
    // Set animation graph
    void setAnimationGraph(AnimationGraph* graph);
    
    // Get simulation state
    bool isSimulationRunning() const { return m_simulationRunning.load(std::memory_order_acquire); }
    float getSimulationTime() const { return m_simulationTime.load(std::memory_order_acquire); }
    
    // Prevent copying
    SimulationThread(const SimulationThread&) = delete;
    SimulationThread& operator=(const SimulationThread&) = delete;
    
private:
    // Thread function
    void simulationThreadFunc();
    
    // Simulation update
    void updateSimulation(float deltaTime);
    
    // Handle commands
    void handleCommand(const SimulationCommand& cmd);
    
    // Save/restore scene state
    void saveSceneState();
    void restoreSceneState();
    
    // Thread
    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_initialized{false};
    
    // Command queue
    ThreadSafeQueue<SimulationCommand> m_commandQueue;
    
    // Triple buffer for scene synchronization
    TripleBuffer<Scene>* m_sceneBuffer = nullptr;
    
    // Simulation state
    std::atomic<bool> m_simulationRunning{false};
    std::atomic<float> m_simulationTime{0.0f};
    bool m_startEventExecuted = false;
    
    // Saved scene for reset
    std::unique_ptr<Scene> m_savedScene;
    
    // Animation graph
    AnimationGraph* m_animationGraph = nullptr;
    std::unique_ptr<GraphExecutor> m_graphExecutor;
};

} // namespace Threading
