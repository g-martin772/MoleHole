#pragma once

#include <vulkan/vulkan.hpp>
#include "VulkanDevice.h"

class VulkanFence {
public:
    VulkanFence(VulkanDevice *device, bool isSignaled = true);
    ~VulkanFence();

    void Reset() const;
    void Wait() const;
    void WaitAndReset() const;

    vk::Fence GetFence() const { return m_Fence; }
private:
    vk::Fence m_Fence;
    VulkanDevice *m_Device = nullptr;
};

class VulkanSemaphore {
public:
    VulkanSemaphore(VulkanDevice *device);
    ~VulkanSemaphore();

    vk::Semaphore GetSemaphore() const { return m_Semaphore; }

private:
    vk::Semaphore m_Semaphore;
    VulkanDevice *m_Device = nullptr;
};
