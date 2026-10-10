#include "PhysicsDebugRenderer.h"
#include "Shader.h"
#include "Camera.h"
#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

void PhysicsDebugRenderer::Init() {
    m_LineShader = std::make_unique<Shader>("../shaders/physics_debug_line.vert", "../shaders/physics_debug_line.frag");
    m_TriangleShader = std::make_unique<Shader>("../shaders/physics_debug_triangle.vert", "../shaders/physics_debug_triangle.frag");

    glGenVertexArrays(1, &m_LineVAO);
    glGenBuffers(1, &m_LineVBO);

    glBindVertexArray(m_LineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
    glBufferData(GL_ARRAY_BUFFER, MAX_LINES * sizeof(float) * 6, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);

    glGenVertexArrays(1, &m_TriangleVAO);
    glGenBuffers(1, &m_TriangleVBO);

    glBindVertexArray(m_TriangleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_TriangleVBO);
    glBufferData(GL_ARRAY_BUFFER, MAX_TRIANGLES * 3 * sizeof(float) * 6, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);

    spdlog::info("PhysicsDebugRenderer initialized");
}

void PhysicsDebugRenderer::Shutdown() {
    if (m_LineVAO) {
        glDeleteVertexArrays(1, &m_LineVAO);
        glDeleteBuffers(1, &m_LineVBO);
        m_LineVAO = 0;
        m_LineVBO = 0;
    }

    if (m_TriangleVAO) {
        glDeleteVertexArrays(1, &m_TriangleVAO);
        glDeleteBuffers(1, &m_TriangleVBO);
        m_TriangleVAO = 0;
        m_TriangleVBO = 0;
    }

    spdlog::info("PhysicsDebugRenderer shutdown");
}

void PhysicsDebugRenderer::Render(const PxRenderBuffer* renderBuffer, Camera* camera) {
    if (!m_Enabled || !camera || !renderBuffer) {
        return;
    }

    static int frameCounter = 0;
    if (frameCounter % 60 == 0) {
        spdlog::debug("PhysX Debug: {} lines, {} triangles, {} points",
                     renderBuffer->getNbLines(),
                     renderBuffer->getNbTriangles(),
                     renderBuffer->getNbPoints());
    }
    frameCounter++;

    if (m_DepthTestEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    if (renderBuffer->getNbLines() > 0) {
        RenderLines(renderBuffer->getLines(), renderBuffer->getNbLines(), camera);
    }

    if (renderBuffer->getNbTriangles() > 0) {
        RenderTriangles(renderBuffer->getTriangles(), renderBuffer->getNbTriangles(), camera);
    }

    if (m_DepthTestEnabled) {
        glEnable(GL_DEPTH_TEST);
    }
}

void PhysicsDebugRenderer::RenderLines(const PxDebugLine* lines, uint32_t count, Camera* camera) {
    if (count == 0) return;

    count = std::min(count, static_cast<uint32_t>(MAX_LINES));

    std::vector<float> vertexData;
    vertexData.reserve(count * 2 * 6);

    for (uint32_t i = 0; i < count; ++i) {
        const PxDebugLine& line = lines[i];

        uint32_t color0 = line.color0;
        float r0 = ((color0 >> 16) & 0xFF) / 255.0f;
        float g0 = ((color0 >> 8) & 0xFF) / 255.0f;
        float b0 = (color0 & 0xFF) / 255.0f;

        uint32_t color1 = line.color1;
        float r1 = ((color1 >> 16) & 0xFF) / 255.0f;
        float g1 = ((color1 >> 8) & 0xFF) / 255.0f;
        float b1 = (color1 & 0xFF) / 255.0f;

        vertexData.push_back(line.pos0.x);
        vertexData.push_back(line.pos0.y);
        vertexData.push_back(line.pos0.z);
        vertexData.push_back(r0);
        vertexData.push_back(g0);
        vertexData.push_back(b0);

        vertexData.push_back(line.pos1.x);
        vertexData.push_back(line.pos1.y);
        vertexData.push_back(line.pos1.z);
        vertexData.push_back(r1);
        vertexData.push_back(g1);
        vertexData.push_back(b1);
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexData.size() * sizeof(float), vertexData.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_LineShader->Bind();
    glm::mat4 viewProj = camera->GetProjectionMatrix() * camera->GetViewMatrix();
    m_LineShader->SetMat4("u_ViewProjection", viewProj);

    glLineWidth(2.0f);

    glBindVertexArray(m_LineVAO);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(count * 2));
    glBindVertexArray(0);

    glLineWidth(1.0f);
}

void PhysicsDebugRenderer::RenderTriangles(const PxDebugTriangle* triangles, uint32_t count, Camera* camera) {
    if (count == 0) return;

    count = std::min(count, static_cast<uint32_t>(MAX_TRIANGLES));

    std::vector<float> vertexData;
    vertexData.reserve(count * 3 * 6);

    for (uint32_t i = 0; i < count; ++i) {
        const PxDebugTriangle& tri = triangles[i];

        uint32_t color0 = tri.color0;
        float r0 = ((color0 >> 16) & 0xFF) / 255.0f;
        float g0 = ((color0 >> 8) & 0xFF) / 255.0f;
        float b0 = (color0 & 0xFF) / 255.0f;

        uint32_t color1 = tri.color1;
        float r1 = ((color1 >> 16) & 0xFF) / 255.0f;
        float g1 = ((color1 >> 8) & 0xFF) / 255.0f;
        float b1 = (color1 & 0xFF) / 255.0f;

        uint32_t color2 = tri.color2;
        float r2 = ((color2 >> 16) & 0xFF) / 255.0f;
        float g2 = ((color2 >> 8) & 0xFF) / 255.0f;
        float b2 = (color2 & 0xFF) / 255.0f;

        vertexData.push_back(tri.pos0.x);
        vertexData.push_back(tri.pos0.y);
        vertexData.push_back(tri.pos0.z);
        vertexData.push_back(r0);
        vertexData.push_back(g0);
        vertexData.push_back(b0);

        vertexData.push_back(tri.pos1.x);
        vertexData.push_back(tri.pos1.y);
        vertexData.push_back(tri.pos1.z);
        vertexData.push_back(r1);
        vertexData.push_back(g1);
        vertexData.push_back(b1);

        vertexData.push_back(tri.pos2.x);
        vertexData.push_back(tri.pos2.y);
        vertexData.push_back(tri.pos2.z);
        vertexData.push_back(r2);
        vertexData.push_back(g2);
        vertexData.push_back(b2);
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_TriangleVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexData.size() * sizeof(float), vertexData.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_TriangleShader->Bind();
    glm::mat4 viewProj = camera->GetProjectionMatrix() * camera->GetViewMatrix();
    m_TriangleShader->SetMat4("u_ViewProjection", viewProj);

    glBindVertexArray(m_TriangleVAO);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count * 3));
    glBindVertexArray(0);
}

