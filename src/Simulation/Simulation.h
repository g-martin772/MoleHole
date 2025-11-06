#pragma once
#include <memory>

#include "Scene.h"
#include "Physics.h"

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

    State GetState() const { return m_State; }
    bool IsRunning() const { return m_State == State::Running; }
    bool IsPaused() const { return m_State == State::Paused; }
    bool IsStopped() const { return m_State == State::Stopped; }

    Scene* GetScene() const;
    float GetSimulationTime() const { return m_SimulationTime; }

    void SetAnimationGraph(AnimationGraph* graph);
    Physics* GetPhysics() const { return m_Physics.get(); }

private:
    std::unique_ptr<Scene> m_Scene;
    std::unique_ptr<Scene> m_SavedScene;
    std::unique_ptr<GraphExecutor> m_GraphExecutor;
    std::unique_ptr<Physics> m_Physics;
    AnimationGraph* m_AnimationGraph;
    State m_State;
    float m_SimulationTime;
    bool m_StartEventExecuted;

    void SaveSceneState() const;
    void RestoreSceneState() const;
    void UpdateSimulation(float deltaTime) const;
};
