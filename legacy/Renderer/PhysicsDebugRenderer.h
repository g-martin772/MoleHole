#pragma once

#include <memory>
#include <glm/glm.hpp>

#ifndef _DEBUG
#define _DEBUG
#endif
#include <PxPhysicsAPI.h>

using namespace physx;

class Shader;
class Camera;

class PhysicsDebugRenderer {
public:
    void Init();
    void Shutdown();
    void Render(const PxRenderBuffer* renderBuffer, Camera* camera);

    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    bool IsEnabled() const { return m_Enabled; }

    void SetDepthTestEnabled(bool enabled) { m_DepthTestEnabled = enabled; }
    bool IsDepthTestEnabled() const { return m_DepthTestEnabled; }

private:
    void RenderLines(const PxDebugLine* lines, uint32_t count, Camera* camera);
    void RenderTriangles(const PxDebugTriangle* triangles, uint32_t count, Camera* camera);

    std::unique_ptr<Shader> m_LineShader;
    std::unique_ptr<Shader> m_TriangleShader;

    unsigned int m_LineVAO = 0;
    unsigned int m_LineVBO = 0;

    unsigned int m_TriangleVAO = 0;
    unsigned int m_TriangleVBO = 0;

    bool m_Enabled = true;
    bool m_DepthTestEnabled = true;

    static constexpr size_t MAX_LINES = 100000;
    static constexpr size_t MAX_TRIANGLES = 100000;
};

