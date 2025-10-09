#include "Simulation.h"
#include <spdlog/spdlog.h>
#include "GraphExecutor.h"
#include "Application/AnimationGraph.h"

Simulation::Simulation() : m_state(State::Stopped), m_simulationTime(0.0f), m_pAnimationGraph(nullptr), m_startEventExecuted(false) {
    m_scene = std::make_unique<Scene>();
    m_savedScene = std::make_unique<Scene>();
}

Simulation::~Simulation() = default;

void Simulation::Update(float deltaTime) {
    if (m_state == State::Running) {
        UpdateSimulation(deltaTime);
        m_simulationTime += deltaTime;
    }
}

void Simulation::Start() {
    if (m_state == State::Stopped) {
        SaveSceneState();
        m_simulationTime = 0.0f;
        m_startEventExecuted = false;

        if (m_pAnimationGraph) {
            m_pGraphExecutor = std::make_unique<GraphExecutor>(m_pAnimationGraph, m_scene.get());
            m_pGraphExecutor->ExecuteStartEvent();
            m_startEventExecuted = true;
        }

        spdlog::info("Simulation started");
    } else if (m_state == State::Paused) {
        spdlog::info("Simulation resumed from pause");
    }
    m_state = State::Running;
}

void Simulation::Stop() {
    if (m_state != State::Stopped) {
        RestoreSceneState();
        m_simulationTime = 0.0f;
        m_state = State::Stopped;
        m_startEventExecuted = false;
        m_pGraphExecutor.reset();
        spdlog::info("Simulation stopped and reset to initial state");
    }
}

void Simulation::Pause() {
    if (m_state == State::Running) {
        m_state = State::Paused;
        spdlog::info("Simulation paused at time: {:.2f}s", m_simulationTime);
    }
}

void Simulation::Reset() {
    RestoreSceneState();
    m_simulationTime = 0.0f;
    if (m_state != State::Stopped) {
        m_state = State::Stopped;
        m_startEventExecuted = false;
        m_pGraphExecutor.reset();
        spdlog::info("Simulation reset to initial state");
    }
}

Scene* Simulation::GetScene() {
    return m_scene.get();
}

void Simulation::SaveSceneState() {
    *m_savedScene = *m_scene;
    spdlog::debug("Scene state saved");
}

void Simulation::RestoreSceneState() {
    *m_scene = *m_savedScene;
    spdlog::debug("Scene state restored");
}

void Simulation::SetAnimationGraph(AnimationGraph* graph) {
    m_pAnimationGraph = graph;
    if (m_state == State::Running && m_pAnimationGraph) {
        m_pGraphExecutor = std::make_unique<GraphExecutor>(m_pAnimationGraph, m_scene.get());
        if (!m_startEventExecuted) {
            m_pGraphExecutor->ExecuteStartEvent();
            m_startEventExecuted = true;
        }
    }
}

void Simulation::UpdateSimulation(float deltaTime) {
    if (m_pGraphExecutor && m_pAnimationGraph) {
        m_pGraphExecutor->ExecuteTickEvent(deltaTime);
    }
}
