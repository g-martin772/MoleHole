#include "GravityGridRenderer.h"
#include "Buffer.h"
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>

void GravityGridRenderer::Init() {
    m_shader = std::make_unique<Shader>("../shaders/plane_grid.vert", "../shaders/plane_grid.frag");
    CreatePlane();
}

void GravityGridRenderer::SetPlaneSize(float size) {
    if (size <= 1.0f) size = 1.0f;
    if (m_planeSize != size) {
        m_planeSize = size;
        CreatePlane();
    }
}

void GravityGridRenderer::SetResolution(int r) {
    r = std::max(4, std::min(1024, r));
    if (m_resolution != r) {
        m_resolution = r;
        CreatePlane();
    }
}

void GravityGridRenderer::CreatePlane() {
    m_vao.reset();
    m_vbo.reset();
    m_ebo.reset();
    m_indexCount = 0;

    int N = std::max(2, m_resolution);
    int vertsPerSide = N + 1;
    int vertexCount = vertsPerSide * vertsPerSide;

    std::vector<float> vertices;
    vertices.reserve(vertexCount * 3);

    float half = m_planeSize * 0.5f;
    for (int z = 0; z <= N; ++z) {
        float tz = (float)z / (float)N;
        float wz = -half + tz * m_planeSize;
        for (int x = 0; x <= N; ++x) {
            float tx = (float)x / (float)N;
            float wx = -half + tx * m_planeSize;
            vertices.push_back(wx);
            vertices.push_back(m_planeY);
            vertices.push_back(wz);
        }
    }

    std::vector<unsigned int> indices;
    indices.reserve(N * N * 6);
    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            int i0 = z * vertsPerSide + x;
            int i1 = i0 + 1;
            int i2 = i0 + vertsPerSide;
            int i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }
    m_indexCount = (int)indices.size();

    m_vao = std::make_unique<VertexArray>();
    m_vao->Bind();
    m_vbo = std::make_unique<VertexBuffer>(vertices.data(), vertices.size() * sizeof(float));
    m_ebo = std::make_unique<IndexBuffer>(indices.data(), indices.size());
    m_vbo->Bind();
    m_ebo->Bind();
    m_vao->EnableAttrib(0, 3, GL_FLOAT, false, 3 * sizeof(float), (void*)0);
    m_vao->Unbind();
}

void GravityGridRenderer::Render(const std::vector<BlackHole>& blackHoles, const Camera& camera, float /*time*/) {
    if (!m_shader || m_indexCount == 0) return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_shader->Bind();

    glm::mat4 vp = camera.GetViewProjectionMatrix();
    m_shader->SetMat4("uVP", vp);
    m_shader->SetFloat("u_planeY", m_planeY);

    int num = (int)std::min<size_t>(blackHoles.size(), 8);
    m_shader->SetInt("u_numBlackHoles", num);
    for (int i = 0; i < num; ++i) {
        const BlackHole& bh = blackHoles[i];
        m_shader->SetVec3("u_blackHolePositions[" + std::to_string(i) + "]", bh.position);
        m_shader->SetFloat("u_blackHoleMasses[" + std::to_string(i) + "]", bh.mass);
    }

    m_shader->SetFloat("u_cellSize", m_cellSize);
    m_shader->SetFloat("u_lineThickness", m_lineThickness);
    m_shader->SetVec3("u_color", m_color);
    m_shader->SetFloat("u_opacity", m_opacity);
    m_vao->Bind();
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, (void*)0);
    m_vao->Unbind();

    m_shader->Unbind();

    glDisable(GL_BLEND);
}
