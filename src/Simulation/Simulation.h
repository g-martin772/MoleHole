#pragma once
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

    void Initialize();
    void Update(float deltaTime);

    void Start();
    void Stop();
    void Pause();
    void Reset();

    State GetState() const { return m_State; }
    bool IsRunning() const { return m_State == State::Running; }
    bool IsPaused() const { return m_State == State::Paused; }
    bool IsStopped() const { return m_State == State::Stopped; }

    void LoadScene(const std::filesystem::path& path);
    void SaveScene(const std::filesystem::path& path);
    void NewScene();

    Scene* GetScene() const;
    float GetSimulationTime() const { return m_SimulationTime; }

    void SetAnimationGraph(AnimationGraph* graph);
    Physics* GetPhysics() const { return m_Physics.get(); }

    const std::vector<ObjectClass>& GetObjectClasses() const { return m_ObjectClasses; }
    const std::vector<SceneObjectDefinition>& GetObjectDefinitions() const { return m_ObjectDefinitions; }

private:
    std::unique_ptr<Scene> m_Scene;
    std::unique_ptr<Scene> m_SavedScene;
    std::unique_ptr<GraphExecutor> m_GraphExecutor;
    std::unique_ptr<Physics> m_Physics;
    AnimationGraph* m_AnimationGraph;
    State m_State;
    float m_SimulationTime;
    bool m_StartEventExecuted;

    std::vector<ObjectClass> m_ObjectClasses;
    std::vector<SceneObjectDefinition> m_ObjectDefinitions;

    void SaveSceneState() const;
    void RestoreSceneState() const;
    void UpdateSimulation(float deltaTime) const;
    void LoadObjectClasses(const std::filesystem::path& path);
    void LoadObjectDefinitions(const std::filesystem::path& path);
};
