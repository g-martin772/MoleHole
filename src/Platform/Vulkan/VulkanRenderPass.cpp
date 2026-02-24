#include "VulkanRenderPass.h"

#include "VulkanApi.h"
#include "VulkanSwapchain.h"
#include <spdlog/spdlog.h>

void VulkanRenderPass::Init(VulkanDevice *device, VulkanSwapChain *swapchain, glm::vec4 canvas, glm::vec4 clearValue,
                            float depthClearValue, uint32_t stencilClearValue) {
    m_Canvas = canvas;
    m_ClearValue = clearValue;
    m_DepthClearValue = depthClearValue;
    m_StencilClearValue = stencilClearValue;
    m_Device = device;

    // Main SubPass
    vk::SubpassDescription subPass = {};
    subPass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;

    // Attachments TODO: Make Attachments configurable
    std::vector<vk::AttachmentDescription> attachments;

    // Color Attachment
    vk::AttachmentDescription colorAttachment = {};
    colorAttachment.format = swapchain->GetImageFormat();
    colorAttachment.samples = vk::SampleCountFlagBits::e1;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eLoad;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;
    attachments.push_back(colorAttachment);

    vk::AttachmentReference colorAttachmentReference = {};
    colorAttachmentReference.attachment = 0;
    colorAttachmentReference.layout = vk::ImageLayout::eColorAttachmentOptimal;

    subPass.colorAttachmentCount = 1;
    subPass.pColorAttachments = &colorAttachmentReference;

    // Depth Attachment
    vk::AttachmentDescription depthAttachment = {};
    depthAttachment.format = swapchain->GetDepthImageFormat();
    depthAttachment.samples = vk::SampleCountFlagBits::e1;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
    depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    attachments.push_back(depthAttachment);

    vk::AttachmentReference depthAttachmentReference = {};
    depthAttachmentReference.attachment = 1;
    depthAttachmentReference.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    subPass.pDepthStencilAttachment = &depthAttachmentReference;

    // TODO: other attachment types (input, resolve, preserve)
    subPass.inputAttachmentCount = 0;
    subPass.pInputAttachments = nullptr;
    subPass.pResolveAttachments = nullptr;
    subPass.preserveAttachmentCount = 0;
    subPass.pPreserveAttachments = nullptr;

    // SubPass Dependencies
    std::vector<vk::SubpassDependency> subPassDependencies;

    vk::SubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask = vk::AccessFlagBits::eNone;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
    subPassDependencies.push_back(dependency);

    // RenderPass
    vk::RenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subPass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(subPassDependencies.size());
    renderPassInfo.pDependencies = subPassDependencies.data();

    try {
        m_RenderPass = m_Device->GetDevice().createRenderPass(renderPassInfo);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to create RenderPass: {}", err.what());
        return;
    }
    spdlog::trace("Created RenderPass: successful");
}

void VulkanRenderPass::Destroy() const {
    m_Device->GetDevice().destroyRenderPass(m_RenderPass);
}

void VulkanRenderPass::Begin(vk::Framebuffer framebuffer, glm::vec2 size, vk::CommandBuffer commandBuffer) const {
    vk::RenderPassBeginInfo beginInfo;
    beginInfo.renderPass = m_RenderPass;
    beginInfo.framebuffer = framebuffer;
    beginInfo.renderArea.offset.x = 0;
    beginInfo.renderArea.offset.y = 0;
    beginInfo.renderArea.extent.width = size.x;
    beginInfo.renderArea.extent.height = size.y;

    const vk::ClearValue clearValues[2] = {
        vk::ClearValue(vk::ClearColorValue(std::array<float, 4>{
            m_ClearValue.r, m_ClearValue.g, m_ClearValue.b, m_ClearValue.a
        })),
        vk::ClearValue(vk::ClearDepthStencilValue(m_DepthClearValue, m_StencilClearValue))
    };

    beginInfo.clearValueCount = 2;
    beginInfo.pClearValues = clearValues;

    commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);
}

void VulkanRenderPass::End(vk::CommandBuffer commandBuffer) {
    commandBuffer.endRenderPass();
}
