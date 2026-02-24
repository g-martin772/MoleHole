#include "VulkanSyncObjects.h"

#include "VulkanDevice.h"
#include <spdlog/spdlog.h>

VulkanFence::VulkanFence(VulkanDevice *device, bool isSignaled) {
    m_Device = device;

    vk::FenceCreateInfo createInfo;
    createInfo.flags = isSignaled ? vk::FenceCreateFlagBits::eSignaled : vk::FenceCreateFlags();

    try {
        m_Fence = device->GetDevice().createFence(createInfo);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to create vulkan fence: {}", err.what());
        return;
    }
    spdlog::trace("Created vulkan fence: successful");
}

VulkanFence::~VulkanFence() {
    m_Device->GetDevice().destroyFence(m_Fence);
}

void VulkanFence::Reset() const {
    const vk::Result result = m_Device->GetDevice().resetFences(1, &m_Fence);
    if (result != vk::Result::eSuccess)
        spdlog::error("Failed to reset vulkan fence: {}", vk::to_string(result));
}

void VulkanFence::Wait() const {
    const vk::Result result = m_Device->GetDevice().waitForFences(1, &m_Fence, VK_TRUE, UINT64_MAX);
    if (result != vk::Result::eSuccess)
        spdlog::error("Failed to wait for vulkan fence: {}", vk::to_string(result));
}

void VulkanFence::WaitAndReset() const {
    Wait();
    Reset();
}

VulkanSemaphore::VulkanSemaphore(VulkanDevice *device) {
    m_Device = device;

    vk::SemaphoreCreateInfo createInfo;
    createInfo.flags = vk::SemaphoreCreateFlags();

    try {
        m_Semaphore = device->GetDevice().createSemaphore(createInfo);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to create vulkan semaphore: {}", err.what());
        return;
    }
    spdlog::trace("Created vulkan semaphore: successful");
}

VulkanSemaphore::~VulkanSemaphore() {
    m_Device->GetDevice().destroySemaphore(m_Semaphore);
}
