#pragma once
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "Shader.h"
#include "Simulation/Scene.h"
#include "Buffer.h"

class ObjectPathsRenderer {
public:
    void Init();
    void Render(const std::vector<BlackHole>& blackHoles, const Camera& camera, float time);

    void SetSimulationDuration(float duration) { m_simulationDuration = duration; }
    void SetTimeStep(float dt) { m_timeStep = dt; }
    void SetLineThickness(float t) { m_lineThickness = t; }
    void SetOpacity(float a) { m_opacity = a; }
    void SetMeshColor(const glm::vec3& c) { m_meshColor = c; }
    void SetSphereColor(const glm::vec3& c) { m_sphereColor = c; }

    float GetSimulationDuration() const { return m_simulationDuration; }
    float GetTimeStep() const { return m_timeStep; }
    float GetLineThickness() const { return m_lineThickness; }
    float GetOpacity() const { return m_opacity; }
    glm::vec3 GetMeshColor() const { return m_meshColor; }
    glm::vec3 GetSphereColor() const { return m_sphereColor; }

private:
    struct TrajectoryPoint {
        glm::vec3 position;
        float time;
    };

    void ComputeTrajectories(const std::vector<BlackHole>& blackHoles);
    void UpdateBuffers();
    glm::vec3 ComputeGravitationalAcceleration(const glm::vec3& position, const std::vector<BlackHole>& blackHoles);

    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<VertexArray> m_vao;
    std::unique_ptr<VertexBuffer> m_vbo;
    
    std::vector<std::vector<TrajectoryPoint>> m_meshTrajectories;
    std::vector<std::vector<TrajectoryPoint>> m_sphereTrajectories;
    std::vector<float> m_lineVertices;
    int m_vertexCount = 0;

    Scene* m_cachedScene = nullptr;
    std::vector<BlackHole> m_cachedBlackHoles;
    
    float m_simulationDuration = 10.0f;  // How far ahead to simulate
    float m_timeStep = 0.05f;            // Time step for simulation
    float m_lineThickness = 2.0f;
    float m_opacity = 0.8f;
    glm::vec3 m_meshColor = glm::vec3(0.2f, 0.8f, 0.2f);    // Green for meshes
    glm::vec3 m_sphereColor = glm::vec3(0.8f, 0.2f, 0.8f);  // Magenta for spheres
};
