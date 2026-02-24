#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "VulkanImage.h"

class VulkanRenderPass;
class VulkanSwapChain;

class VulkanFrameBuffer {
public:
    void Create(VulkanDevice *device, glm::vec2 size, std::vector<vk::ImageView> imageViews,
                VulkanRenderPass *renderPass);
    void CreateFromSwapchain(VulkanDevice *device, VulkanSwapChain *swapChain, VulkanRenderPass *renderPass);
    void Destroy();
    vk::Framebuffer GetFrameBuffer(uint32_t index) const { return m_FrameBuffers[index]; }
private:
    std::vector<vk::Framebuffer> m_FrameBuffers;
    VulkanRenderPass *m_RenderPass = nullptr;
    VulkanDevice *m_Device = nullptr;
};
