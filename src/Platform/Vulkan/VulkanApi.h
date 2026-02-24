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
    VulkanDevice GetDevice() const { return m_Device; }
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
