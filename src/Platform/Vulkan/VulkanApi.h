#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

#include "VulkanInstance.h"
#include "VulkanDevice.h"
#include "VulkanCommandBuffer.h"
#include "VulkanFramebuffer.h"
#include "VulkanRenderPass.h"
#include "VulkanSwapchain.h"
#include "VulkanSyncObjects.h"

class VulkanApi {
public:
    void Init();
    void Shutdown();

    bool BeginFrame();
    void EndFrame();

    void OnResize(glm::vec2 newSize);
    void SetClearColor(glm::vec4 color);
    void SetVSync(bool enabled);

    VulkanInstance GetInstance() const { return m_Instance; }
    VulkanDevice& GetDevice() { return m_Device; }
    const VulkanDevice& GetDevice() const { return m_Device; }
    VulkanRenderPass& GetMainRenderPass() { return m_MainRenderPass; }
    VulkanCommandPool& GetGraphicsCommandPool() { return m_GraphicsCommandPool; }
    VulkanSwapChain& GetSwapChain() { return m_SwapChain; }

    vk::CommandBuffer GetCurrentCommandBuffer() const {
        uint32_t currentFrame = m_SwapChain.GetCurrentImageIndex();
        if (currentFrame < m_RenderCommandBuffers.size())
            return m_RenderCommandBuffers[currentFrame]->GetCommandBuffer();
        return nullptr;
    }
private:
    VulkanInstance m_Instance;
    VulkanDevice m_Device;
    vk::SurfaceKHR m_Surface;
    vk::DescriptorPool m_ImGuiDescriptorPool;
    VulkanRenderPass m_MainRenderPass;
    VulkanSwapChain m_SwapChain;
    VulkanCommandPool m_GraphicsCommandPool;
    Ref<VulkanCommandBuffer> m_MainCommandBuffer;
    VulkanFrameBuffer m_MainFrameBuffer;
    std::vector<Ref<VulkanCommandBuffer> > m_RenderCommandBuffers;
    std::vector<VulkanFence> m_InFlightFences;
    std::vector<VulkanSemaphore> m_RenderFinishedSemaphores;
    std::vector<VulkanSemaphore> m_ImageAvailableSemaphores;
    friend class VulkanRenderCommand;
};
