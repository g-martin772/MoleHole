#pragma once
#include <memory>
#include <vector>
#include <deque>
#include <glm/glm.hpp>
#include "Shader.h"
#include "Simulation/Scene.h"
#include "Buffer.h"

class ObjectPathsRenderer {
public:
    void Init();
    void Render(const std::vector<BlackHole>& blackHoles, const Camera& camera, float time);
    void RecordCurrentPositions(Scene* scene);
    void ClearHistories();

    void SetMaxHistorySize(size_t size) { m_maxHistorySize = size; }
    void SetLineThickness(float t) { m_lineThickness = t; }
    void SetOpacity(float a) { m_opacity = a; }
    void SetMeshColor(const glm::vec3& c) { m_meshColor = c; }
    void SetSphereColor(const glm::vec3& c) { m_sphereColor = c; }

    size_t GetMaxHistorySize() const { return m_maxHistorySize; }
    float GetLineThickness() const { return m_lineThickness; }
    float GetOpacity() const { return m_opacity; }
    glm::vec3 GetMeshColor() const { return m_meshColor; }
    glm::vec3 GetSphereColor() const { return m_sphereColor; }

private:
    struct HistoricalPath {
        std::deque<glm::vec3> positions;
    };

    void UpdateBuffers();

    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<VertexArray> m_vao;
    std::unique_ptr<VertexBuffer> m_vbo;
    
    std::vector<HistoricalPath> m_meshHistories;
    std::vector<HistoricalPath> m_sphereHistories;
    std::vector<float> m_lineVertices;
    int m_vertexCount = 0;

    Scene* m_cachedScene = nullptr;
    
    size_t m_maxHistorySize = 2000;
    float m_lineThickness = 2.0f;
    float m_opacity = 0.8f;
    glm::vec3 m_meshColor = glm::vec3(0.2f, 0.8f, 0.2f);
    glm::vec3 m_sphereColor = glm::vec3(0.8f, 0.2f, 0.8f);
};
