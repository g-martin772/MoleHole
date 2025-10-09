#pragma once
#include <memory>

#include "Scene.h"

class AnimationGraph;
class GraphExecutor;

class Simulation {
public:
    enum class State {
        Stopped,
        Running,
        Paused
    };

    Simulation();
    ~Simulation();

    void Update(float deltaTime);

    void Start();
    void Stop();
    void Pause();
    void Reset();

    State GetState() const { return m_state; }
    bool IsRunning() const { return m_state == State::Running; }
    bool IsPaused() const { return m_state == State::Paused; }
    bool IsStopped() const { return m_state == State::Stopped; }

    Scene* GetScene();
    float GetSimulationTime() const { return m_simulationTime; }

    void SetAnimationGraph(AnimationGraph* graph);

private:
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<Scene> m_savedScene;
    std::unique_ptr<GraphExecutor> m_pGraphExecutor;
    AnimationGraph* m_pAnimationGraph;
    State m_state;
    float m_simulationTime;
    bool m_startEventExecuted;

    void SaveSceneState();
    void RestoreSceneState();
    void UpdateSimulation(float deltaTime);
};
