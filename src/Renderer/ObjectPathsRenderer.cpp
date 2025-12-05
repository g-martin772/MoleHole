#include "ObjectPathsRenderer.h"
#include "Buffer.h"
#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include "Camera.h"
#include "Application/Application.h"
#include <spdlog/spdlog.h>

void ObjectPathsRenderer::Init() {
    m_shader = std::make_unique<Shader>("../shaders/object_paths.vert", "../shaders/object_paths.frag");
    
    m_vao = std::make_unique<VertexArray>();
    m_vao->Bind();
    
    // Create an empty VBO that will be updated each frame
    m_vbo = std::make_unique<VertexBuffer>(nullptr, 0);
    m_vbo->Bind();
    
    // Position attribute (x, y, z)
    m_vao->EnableAttrib(0, 3, GL_FLOAT, false, 3 * sizeof(float), (void*)0);
    m_vao->Unbind();
}

glm::vec3 ObjectPathsRenderer::ComputeGravitationalAcceleration(const glm::vec3& position, const std::vector<BlackHole>& blackHoles) {
    glm::vec3 acceleration(0.0f);
    
    for (const auto& bh : blackHoles) {
        glm::vec3 r = bh.position - position;
        float distSq = glm::dot(r, r);
        
        if (distSq < 0.01f) continue; // Avoid singularity
        
        float dist = std::sqrt(distSq);
        float forceMagnitude = (bh.mass * 100.0f) / distSq; // Scaled for visualization
        
        acceleration += (r / dist) * forceMagnitude;
    }
    
    return acceleration;
}

void ObjectPathsRenderer::ComputeTrajectories(const std::vector<BlackHole>& blackHoles) {
    Scene* scene = Application::GetSimulation().GetScene();
    if (!scene) return;
    
    m_meshTrajectories.clear();
    m_sphereTrajectories.clear();
    
    spdlog::info("Computing trajectories: {} meshes, {} spheres, {} black holes", 
                 scene->meshes.size(), scene->spheres.size(), blackHoles.size());
    
    // Compute trajectories for meshes
    for (const auto& mesh : scene->meshes) {
        std::vector<TrajectoryPoint> trajectory;
        glm::vec3 pos = mesh.position;
        glm::vec3 vel = mesh.velocity;
        
        for (float t = 0.0f; t < m_simulationDuration; t += m_timeStep) {
            trajectory.push_back({pos, t});
            
            // Compute acceleration
            glm::vec3 accel = ComputeGravitationalAcceleration(pos, blackHoles);
            
            // Velocity Verlet integration
            vel += accel * m_timeStep;
            pos += vel * m_timeStep;
        }
        
        if (!trajectory.empty()) {
            m_meshTrajectories.push_back(trajectory);
        }
    }
    
    // Compute trajectories for spheres
    for (const auto& sphere : scene->spheres) {
        std::vector<TrajectoryPoint> trajectory;
        glm::vec3 pos = sphere.position;
        glm::vec3 vel = sphere.velocity;
        
        spdlog::info("Sphere trajectory: initial pos=({}, {}, {}), vel=({}, {}, {})", 
                     pos.x, pos.y, pos.z, vel.x, vel.y, vel.z);
        
        for (float t = 0.0f; t < m_simulationDuration; t += m_timeStep) {
            trajectory.push_back({pos, t});
            
            // Compute acceleration
            glm::vec3 accel = ComputeGravitationalAcceleration(pos, blackHoles);
            
            // Velocity Verlet integration
            vel += accel * m_timeStep;
            pos += vel * m_timeStep;
        }
        
        spdlog::info("Sphere trajectory computed: {} points", trajectory.size());
        
        if (!trajectory.empty()) {
            m_sphereTrajectories.push_back(trajectory);
        }
    }
}

void ObjectPathsRenderer::UpdateBuffers() {
    m_lineVertices.clear();
    
    // Add mesh trajectories to line vertices
    for (const auto& trajectory : m_meshTrajectories) {
        for (const auto& point : trajectory) {
            m_lineVertices.push_back(point.position.x);
            m_lineVertices.push_back(point.position.y);
            m_lineVertices.push_back(point.position.z);
        }
    }
    
    // Add sphere trajectories to line vertices
    for (const auto& trajectory : m_sphereTrajectories) {
        for (const auto& point : trajectory) {
            m_lineVertices.push_back(point.position.x);
            m_lineVertices.push_back(point.position.y);
            m_lineVertices.push_back(point.position.z);
        }
    }
    
    m_vertexCount = static_cast<int>(m_lineVertices.size() / 3);
    
    spdlog::info("UpdateBuffers: {} vertices total", m_vertexCount);
    
    if (m_vertexCount > 0) {
        m_vbo->Bind();
        glBufferData(GL_ARRAY_BUFFER, m_lineVertices.size() * sizeof(float), m_lineVertices.data(), GL_DYNAMIC_DRAW);
    }
}

void ObjectPathsRenderer::Render(const std::vector<BlackHole>& blackHoles, const Camera& camera, float /*time*/) {
    if (!m_shader) return;
    
    Scene* scene = Application::GetSimulation().GetScene();
    if (!scene) return;
    
    // Check if we need to recompute trajectories
    bool needsUpdate = (scene != m_cachedScene) || (blackHoles != m_cachedBlackHoles);
    
    if (needsUpdate) {
        ComputeTrajectories(blackHoles);
        UpdateBuffers();
        m_cachedScene = scene;
        m_cachedBlackHoles = blackHoles;
    }
    
    if (m_vertexCount == 0) return;
    
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(m_lineThickness);
    
    m_shader->Bind();
    
    glm::mat4 vp = camera.GetViewProjectionMatrix();
    m_shader->SetMat4("uVP", vp);
    m_shader->SetFloat("u_opacity", m_opacity);
    
    m_vao->Bind();
    
    // Render mesh trajectories
    int offset = 0;
    m_shader->SetVec3("u_color", m_meshColor);
    for (const auto& trajectory : m_meshTrajectories) {
        if (trajectory.size() > 1) {
            glDrawArrays(GL_LINE_STRIP, offset, static_cast<int>(trajectory.size()));
        }
        offset += static_cast<int>(trajectory.size());
    }
    
    // Render sphere trajectories
    m_shader->SetVec3("u_color", m_sphereColor);
    for (const auto& trajectory : m_sphereTrajectories) {
        if (trajectory.size() > 1) {
            glDrawArrays(GL_LINE_STRIP, offset, static_cast<int>(trajectory.size()));
        }
        offset += static_cast<int>(trajectory.size());
    }
    
    m_vao->Unbind();
    m_shader->Unbind();
    
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
