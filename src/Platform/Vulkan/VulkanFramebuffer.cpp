#include "VulkanFramebuffer.h"

#include "VulkanRenderPass.h"
#include "VulkanSwapchain.h"
#include "VulkanDevice.h"
#include <spdlog/spdlog.h>



// TODO: Support multiple attachments
void VulkanFrameBuffer::Create(VulkanDevice *device, glm::vec2 size, std::vector<vk::ImageView> imageViews,
                               VulkanRenderPass *renderPass) {
    m_RenderPass = renderPass;
    m_Device = device;

    for (auto &imageView: imageViews) {
        vk::FramebufferCreateInfo createInfo;
        createInfo.renderPass = m_RenderPass->GetRenderPass();
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &imageView;
        createInfo.width = size.x;
        createInfo.height = size.y;
        createInfo.layers = 1;

        try {
            m_FrameBuffers.push_back(m_Device->GetDevice().createFramebuffer(createInfo));
        } catch (const vk::SystemError &err) {
            spdlog::error("Failed to create vulkan frame buffer: {}", err.what());
            return;
        }
        spdlog::trace("Created vulkan frame buffer: successful");
    }
}

void VulkanFrameBuffer::CreateFromSwapchain(VulkanDevice *device, VulkanSwapChain *swapChain,
                                            VulkanRenderPass *renderPass) {
    m_RenderPass = renderPass;
    m_Device = device;

    const auto imageViews = swapChain->GetImageViews();

    for (auto &imageView: imageViews) {
        vk::ImageView attachments[2] = {imageView, swapChain->GetDepthImageView()};

        vk::FramebufferCreateInfo createInfo;
        createInfo.renderPass = m_RenderPass->GetRenderPass();
        createInfo.attachmentCount = 2;
        createInfo.pAttachments = attachments;
        createInfo.width = swapChain->GetExtent().width;
        createInfo.height = swapChain->GetExtent().height;
        createInfo.layers = 1;

        try {
            m_FrameBuffers.push_back(m_Device->GetDevice().createFramebuffer(createInfo));
        } catch (const vk::SystemError &err) {
            spdlog::error("Failed to create vulkan frame buffer: {}", err.what());
            return;
        }
        spdlog::trace("Created vulkan frame buffer: successful");
    }
}

void VulkanFrameBuffer::Destroy() {
    for (vk::Framebuffer framebuffer: m_FrameBuffers)
        m_Device->GetDevice().destroyFramebuffer(framebuffer);
}
