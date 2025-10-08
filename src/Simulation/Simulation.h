#pragma once
#include <memory>

#include "Scene.h"

class Simulation {
public:
    enum class State {
        Stopped,
        Running,
        Paused
    };

    Simulation();
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

private:
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<Scene> m_savedScene;
    State m_state;
    float m_simulationTime;

    void SaveSceneState();
    void RestoreSceneState();
    void UpdateSimulation(float deltaTime);
};
