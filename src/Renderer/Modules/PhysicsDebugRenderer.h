#pragma once

#ifndef _DEBUG
#define _DEBUG
#endif
#include <PxPhysicsAPI.h>

// #include <Renderer/Interface/Shader.h>
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Platform/Vulkan/VulkanBuffer.h"

using namespace physx;

class Camera;

#include "Platform/Vulkan/VulkanRenderPass.h"

class PhysicsDebugRenderer {
public:
    void Init(VulkanDevice* device, VulkanRenderPass* renderPass);
    void Shutdown();
    void Render(const PxRenderBuffer* renderBuffer, Camera* camera, vk::CommandBuffer cmd);

    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    bool IsEnabled() const { return m_Enabled; }

    void SetDepthTestEnabled(bool enabled) { m_DepthTestEnabled = enabled; }
    bool IsDepthTestEnabled() const { return m_DepthTestEnabled; }

private:
    void RenderLines(const PxDebugLine* lines, uint32_t count, Camera* camera, vk::CommandBuffer cmd);
    void RenderTriangles(const PxDebugTriangle* triangles, uint32_t count, Camera* camera, vk::CommandBuffer cmd);

    VulkanDevice* m_Device = nullptr;

    std::unique_ptr<VulkanShader> m_LineShader;
    std::unique_ptr<VulkanPipeline> m_LinePipeline;
    std::unique_ptr<VulkanBuffer> m_LineVBO;

    std::unique_ptr<VulkanShader> m_TriangleShader;
    std::unique_ptr<VulkanPipeline> m_TrianglePipeline;
    std::unique_ptr<VulkanBuffer> m_TriangleVBO;

    bool m_Enabled = true;
    bool m_DepthTestEnabled = true;

    static constexpr size_t MAX_LINES = 100000;
    static constexpr size_t MAX_TRIANGLES = 100000;
};

