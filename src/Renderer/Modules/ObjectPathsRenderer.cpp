#include "ObjectPathsRenderer.h"
#include "Renderer/Interface/Buffer.h"
#include "Simulation/Scene.h"
#include <glad/gl.h>
#include "Renderer/Interface/Camera.h"
#include "Application/Application.h"

void ObjectPathsRenderer::Init() {
    m_shader = std::make_unique<Shader>("../shaders/object_paths.vert", "../shaders/object_paths.frag");
    
    m_vao = std::make_unique<VertexArray>();
    m_vao->Bind();
    
    m_vbo = std::make_unique<VertexBuffer>(nullptr, 0);
    m_vbo->Bind();
    
    m_vao->EnableAttrib(0, 3, GL_FLOAT, false, 3 * sizeof(float), (void*)0);
    m_vao->Unbind();
}

void ObjectPathsRenderer::RecordCurrentPositions(Scene* scene) {
    if (!scene) return;
    
    if (scene != m_cachedScene) {
        ClearHistories();
        m_cachedScene = scene;
    }

    size_t meshCount = 0;
    size_t sphereCount = 0;
    for (const auto& obj : scene->objects) {
        if (obj.HasClass("Mesh")) meshCount++;
        if (obj.HasClass("Sphere")) sphereCount++;
    }

    if (m_meshHistories.size() != meshCount) {
        m_meshHistories.resize(meshCount);
    }
    
    if (m_sphereHistories.size() != sphereCount) {
        m_sphereHistories.resize(sphereCount);
    }

    size_t meshIdx = 0;
    for (const auto& obj : scene->objects) {
        if (obj.HasClass("Mesh") && meshIdx < m_meshHistories.size()) {
            ParameterHandle posHandle("Entity.Position");
            auto pos = obj.GetParameter(posHandle);

            if (std::holds_alternative<glm::vec3>(pos)) {
                auto& history = m_meshHistories[meshIdx];
                history.positions.push_back(std::get<glm::vec3>(pos));

                if (history.positions.size() > m_maxHistorySize) {
                    history.positions.pop_front();
                }
            }
            meshIdx++;
        }
    }

    size_t sphereIdx = 0;
    for (const auto& obj : scene->objects) {
        if (obj.HasClass("Sphere") && sphereIdx < m_sphereHistories.size()) {
            ParameterHandle posHandle("Entity.Position");
            auto pos = obj.GetParameter(posHandle);

            if (std::holds_alternative<glm::vec3>(pos)) {
                auto& history = m_sphereHistories[sphereIdx];
                history.positions.push_back(std::get<glm::vec3>(pos));

                if (history.positions.size() > m_maxHistorySize) {
                    history.positions.pop_front();
                }
            }
            sphereIdx++;
        }
    }
}

void ObjectPathsRenderer::ClearHistories() {
    m_meshHistories.clear();
    m_sphereHistories.clear();
    m_cachedScene = nullptr;
}

void ObjectPathsRenderer::UpdateBuffers() {
    m_lineVertices.clear();
    
    for (const auto& history : m_meshHistories) {
        for (const auto& pos : history.positions) {
            m_lineVertices.push_back(pos.x);
            m_lineVertices.push_back(pos.y);
            m_lineVertices.push_back(pos.z);
        }
    }
    
    for (const auto& history : m_sphereHistories) {
        for (const auto& pos : history.positions) {
            m_lineVertices.push_back(pos.x);
            m_lineVertices.push_back(pos.y);
            m_lineVertices.push_back(pos.z);
        }
    }
    
    m_vertexCount = static_cast<int>(m_lineVertices.size() / 3);
    
    if (m_vertexCount > 0) {
        m_vbo->Bind();
        glBufferData(GL_ARRAY_BUFFER, m_lineVertices.size() * sizeof(float), m_lineVertices.data(), GL_DYNAMIC_DRAW);
    }
}

void ObjectPathsRenderer::Render(const Scene& /*scene*/, const Camera& camera, float /*time*/) {
    if (!m_shader) return;
    
    auto& simulation = Application::GetSimulation();
    if (!simulation.IsRunning()) {
        return;
    }
    
    Scene* scene = Application::GetSimulation().GetScene();
    if (!scene) return;
    
    UpdateBuffers();
    
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
    
    int offset = 0;
    m_shader->SetVec3("u_color", m_meshColor);
    for (const auto& history : m_meshHistories) {
        if (history.positions.size() > 1) {
            glDrawArrays(GL_LINE_STRIP, offset, static_cast<int>(history.positions.size()));
        }
        offset += static_cast<int>(history.positions.size());
    }
    
    m_shader->SetVec3("u_color", m_sphereColor);
    for (const auto& history : m_sphereHistories) {
        if (history.positions.size() > 1) {
            glDrawArrays(GL_LINE_STRIP, offset, static_cast<int>(history.positions.size()));
        }
        offset += static_cast<int>(history.positions.size());
    }
    
    m_vao->Unbind();
    m_shader->Unbind();
    
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
