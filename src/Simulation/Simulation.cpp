#include "Simulation.h"
#include <spdlog/spdlog.h>
#include "GraphExecutor.h"
#include "Application/AnimationGraph.h"
#include "Application/Application.h"

Simulation::Simulation() : m_State(State::Stopped), m_SimulationTime(0.0f), m_AnimationGraph(nullptr), m_StartEventExecuted(false) {
    m_Scene = std::make_unique<Scene>();
    m_SavedScene = std::make_unique<Scene>();
    m_Physics = std::make_unique<Physics>();
    m_Physics->Init();
}

Simulation::~Simulation() {
    m_Physics->Shutdown();
    m_Physics.reset();
    m_Scene.reset();
    m_SavedScene.reset();
};

void Simulation::Update(float deltaTime) {
    if (m_State == State::Running) {
        UpdateSimulation(deltaTime);
        m_SimulationTime += deltaTime;
    }
}

void Simulation::Start() {
    if (m_State == State::Stopped) {
        SaveSceneState();
        m_SimulationTime = 0.0f;
        m_StartEventExecuted = false;

        if (m_AnimationGraph) {
            m_GraphExecutor = std::make_unique<GraphExecutor>(m_AnimationGraph, m_Scene.get());
            m_GraphExecutor->ExecuteStartEvent();
            m_StartEventExecuted = true;
        }

        spdlog::info("Simulation started");
    } else if (m_State == State::Paused) {
        spdlog::info("Simulation resumed from pause");
    }
    m_State = State::Running;
    m_Physics->SetScene(m_Scene.get());
}

void Simulation::Stop() {
    if (m_State != State::Stopped) {
        RestoreSceneState();
        m_SimulationTime = 0.0f;
        m_State = State::Stopped;
        m_StartEventExecuted = false;
        m_GraphExecutor.reset();
        
        auto& renderer = Application::GetRenderer();
        if (auto* pathsRenderer = renderer.GetObjectPathsRenderer()) {
            pathsRenderer->ClearHistories();
        }
        
        spdlog::info("Simulation stopped and reset to initial state");
    }
}

void Simulation::Pause() {
    if (m_State == State::Running) {
        m_State = State::Paused;
        spdlog::info("Simulation paused at time: {:.2f}s", m_SimulationTime);
    }
}

void Simulation::Reset() {
    RestoreSceneState();
    m_SimulationTime = 0.0f;
    if (m_State != State::Stopped) {
        m_State = State::Stopped;
        m_StartEventExecuted = false;
        m_GraphExecutor.reset();
        
        auto& renderer = Application::GetRenderer();
        if (auto* pathsRenderer = renderer.GetObjectPathsRenderer()) {
            pathsRenderer->ClearHistories();
        }
        
        spdlog::info("Simulation reset to initial state");
    }
}

Scene* Simulation::GetScene() const {
    return m_Scene.get();
}

void Simulation::SaveSceneState() const {
    *m_SavedScene = *m_Scene;
    spdlog::debug("Scene state saved");
}

void Simulation::RestoreSceneState() const {
    *m_Scene = *m_SavedScene;
    spdlog::debug("Scene state restored");
}

void Simulation::SetAnimationGraph(AnimationGraph* graph) {
    m_AnimationGraph = graph;
    if (m_State == State::Running && m_AnimationGraph) {
        m_GraphExecutor = std::make_unique<GraphExecutor>(m_AnimationGraph, m_Scene.get());
        if (!m_StartEventExecuted) {
            m_GraphExecutor->ExecuteStartEvent();
            m_StartEventExecuted = true;
        }
    }
}

void Simulation::UpdateSimulation(float deltaTime) const {
    if (m_GraphExecutor && m_AnimationGraph) {
        m_GraphExecutor->ExecuteTickEvent(deltaTime);
    }
    m_Physics->Update(deltaTime);
    m_Physics->Apply();
    
    auto& renderer = Application::GetRenderer();
    if (auto* pathsRenderer = renderer.GetObjectPathsRenderer()) {
        pathsRenderer->RecordCurrentPositions(m_Scene.get());
    }
}
