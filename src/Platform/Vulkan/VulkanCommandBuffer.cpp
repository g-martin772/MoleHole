#include "VulkanCommandBuffer.h"

#include "VulkanDevice.h"
#include <spdlog/spdlog.h>

VulkanCommandBuffer::~VulkanCommandBuffer() {
    if (!m_IsSingleUse)
        Free();
}

void VulkanCommandBuffer::Begin() const {
    vk::CommandBufferBeginInfo beginInfo;
    if (m_IsSingleUse)
        beginInfo.flags |= vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    if (m_RenderPassContinue)
        beginInfo.flags |= vk::CommandBufferUsageFlagBits::eRenderPassContinue;
    if (m_SimultaneousUse)
        beginInfo.flags |= vk::CommandBufferUsageFlagBits::eSimultaneousUse;
    try {
        m_CommandBuffer.begin(beginInfo);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to begin vulkan command buffer: {}", err.what());
        return;
    }
}

void VulkanCommandBuffer::End() const {
    try {
        m_CommandBuffer.end();
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to end vulkan command buffer: {}", err.what());
        return;
    }
}

void VulkanCommandBuffer::Submit(vk::Queue target)
{
    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_CommandBuffer;

    try {
        vk::Result result = target.submit(1, &submitInfo, nullptr);
        if (result != vk::Result::eSuccess) {
            spdlog::error("Failed to submit vulkan command buffer: {}", vk::to_string(result));
            return;
        }
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to submit vulkan command buffer: {}", err.what());
        return;
    }

    if (m_IsSingleUse) {
        target.waitIdle();
        Free();
    }
}

void VulkanCommandBuffer::Submit(vk::Queue target,
                                 vk::Semaphore waitSemaphore,
                                 vk::Semaphore signalSemaphore,
                                 vk::Fence fence,
                                 vk::PipelineStageFlags waitStage)
{
    vk::SubmitInfo submitInfo;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &waitSemaphore;
    submitInfo.pWaitDstStageMask    = &waitStage;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &m_CommandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &signalSemaphore;

    try {
        vk::Result result = target.submit(1, &submitInfo, fence);
        if (result != vk::Result::eSuccess) {
            spdlog::error("Failed to submit vulkan command buffer: {}", vk::to_string(result));
            return;
        }
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to submit vulkan command buffer: {}", err.what());
    }
}

void VulkanCommandBuffer::Free() {
    if (m_Allocated) {
        m_Device->GetDevice().freeCommandBuffers(m_CommandPool, 1, &m_CommandBuffer);
        m_Allocated = false;
    }
}

void VulkanCommandPool::Init(VulkanDevice *device, const uint32_t queueFamilyIndex,
                             const vk::CommandPoolCreateFlags flags) {
    m_Device = device;

    vk::CommandPoolCreateInfo createInfo;
    createInfo.flags = flags;
    createInfo.queueFamilyIndex = queueFamilyIndex;

    try {
        m_CommandPool = m_Device->GetDevice().createCommandPool(createInfo);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to create vulkan command pool: {}", err.what());
        return;
    }
    spdlog::trace("Created vulkan command pool: succesful");
}

void VulkanCommandPool::Destroy() const {
    m_Device->GetDevice().destroyCommandPool(m_CommandPool);
}

Ref<VulkanCommandBuffer> VulkanCommandPool::AllocateCommandBuffer(bool renderPassContinue,
                                                                     bool simultaneousUse) const {
    Ref<VulkanCommandBuffer> commandBuffer = CreateRef<VulkanCommandBuffer>();
    commandBuffer->m_Device = m_Device;
    commandBuffer->m_CommandPool = m_CommandPool;
    commandBuffer->m_SimultaneousUse = simultaneousUse;
    commandBuffer->m_RenderPassContinue = renderPassContinue;
    commandBuffer->m_Allocated = true;

    vk::CommandBufferAllocateInfo allocateInfo;
    allocateInfo.level = vk::CommandBufferLevel::ePrimary;
    allocateInfo.commandBufferCount = 1;
    allocateInfo.commandPool = m_CommandPool;

    try {
        commandBuffer->m_CommandBuffer = m_Device->GetDevice().allocateCommandBuffers(allocateInfo)[0];
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to allocate vulkan command buffer: {}", err.what());
        return nullptr;
    }
    spdlog::trace("Allocated vulkan command buffer: successful");

    return commandBuffer;
}

Ref<VulkanCommandBuffer> VulkanCommandPool::AllocateSingleUseCommandBuffer() const {
    Ref<VulkanCommandBuffer> buffer = AllocateCommandBuffer();
    buffer->m_IsSingleUse = true;
    return buffer;
}
