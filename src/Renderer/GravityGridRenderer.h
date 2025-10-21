#pragma once
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "Shader.h"
#include "Simulation/Scene.h"
#include "Buffer.h"

class GravityGridRenderer {
public:
    void Init();
    void Render(const std::vector<BlackHole>& blackHoles, const Camera& camera, float time);

    void SetPlaneY(float y) { m_planeY = y; }
    void SetPlaneSize(float size);
    void SetCellSize(float size) { m_cellSize = size; }
    void SetOpacity(float a) { m_opacity = a; }
    void SetResolution(int r);
    void SetLineThickness(float t) { m_lineThickness = t; }
    void SetColor(const glm::vec3& c) { m_color = c; }

    float GetPlaneY() const { return m_planeY; }
    float GetPlaneSize() const { return m_planeSize; }
    float GetCellSize() const { return m_cellSize; }
    float GetOpacity() const { return m_opacity; }
    int GetResolution() const { return m_resolution; }
    float GetLineThickness() const { return m_lineThickness; }
    glm::vec3 GetColor() const { return m_color; }

private:
    void CreatePlane();

    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<VertexArray> m_vao;
    std::unique_ptr<VertexBuffer> m_vbo;
    std::unique_ptr<IndexBuffer> m_ebo;
    int m_indexCount = 0;

    float m_planeY = -5.0f;
    float m_planeSize = 200.0f;
    float m_cellSize = 2.0f;
    float m_opacity = 0.7f;

    int m_resolution = 1028;
    float m_lineThickness = 0.03f;
    glm::vec3 m_color = glm::vec3(0.1f, 0.1f, 0.8f);
};
