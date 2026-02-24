#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include "VulkanInstance.h"
#include "VulkanDevice.h"
#include "VulkanCommandBuffer.h"
#include "VulkanFrameBuffer.h"
#include "VulkanRenderPass.h"
#include "VulkanSwapChain.h"
#include "VulkanSyncObjects.h"
#include "Renderer/Renderer.h"

class VulkanApi {
public:
    void Init();

    void Shutdown();

    bool BeginFrame();

    void EndFrame();

    void OnResize(glm::vec2 newSize);

    void SetClearColor(glm::vec4 color);

    VulkanInstance GetInstance() const { return m_Instance; }
    VulkanDevice GetDevice() const { return m_Device; }

private:
    VulkanInstance m_Instance;
    VulkanDevice m_Device;
    vk::SurfaceKHR m_Surface;
    VulkanRenderPass m_MainRenderPass;
    VulkanSwapChain m_SwapChain;
    VulkanCommandPool m_GraphicsCommandPool;
    Shared<VulkanCommandBuffer> m_MainCommandBuffer;
    VulkanFrameBuffer m_MainFrameBuffer;
    std::vector<Shared<VulkanCommandBuffer> > m_RenderCommandBuffers;
    std::vector<VulkanFence> m_InFlightFences;
    std::vector<VulkanSemaphore> m_RenderFinishedSemaphores;
    std::vector<VulkanSemaphore> m_ImageAvailableSemaphores;
    friend class VulkanRenderCommand;
};
