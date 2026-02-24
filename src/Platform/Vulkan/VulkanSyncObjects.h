#pragma once

#include <vulkan/vulkan.hpp>
#include "VulkanDevice.h"

class VulkanFence {
public:
    VulkanFence(VulkanDevice *device, bool isSignaled = true);
    ~VulkanFence();

    VulkanFence(const VulkanFence&) = delete;
    VulkanFence& operator=(const VulkanFence&) = delete;

    VulkanFence(VulkanFence&& other) noexcept
        : m_Fence(other.m_Fence), m_Device(other.m_Device)
    { other.m_Fence = VK_NULL_HANDLE; other.m_Device = nullptr; }

    VulkanFence& operator=(VulkanFence&& other) noexcept {
        if (this != &other) {
            if (m_Fence && m_Device) m_Device->GetDevice().destroyFence(m_Fence);
            m_Fence = other.m_Fence; m_Device = other.m_Device;
            other.m_Fence = VK_NULL_HANDLE; other.m_Device = nullptr;
        }
        return *this;
    }

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

    VulkanSemaphore(const VulkanSemaphore&) = delete;
    VulkanSemaphore& operator=(const VulkanSemaphore&) = delete;

    VulkanSemaphore(VulkanSemaphore&& other) noexcept
        : m_Semaphore(other.m_Semaphore), m_Device(other.m_Device)
    { other.m_Semaphore = VK_NULL_HANDLE; other.m_Device = nullptr; }

    VulkanSemaphore& operator=(VulkanSemaphore&& other) noexcept {
        if (this != &other) {
            if (m_Semaphore && m_Device) m_Device->GetDevice().destroySemaphore(m_Semaphore);
            m_Semaphore = other.m_Semaphore; m_Device = other.m_Device;
            other.m_Semaphore = VK_NULL_HANDLE; other.m_Device = nullptr;
        }
        return *this;
    }

    vk::Semaphore GetSemaphore() const { return m_Semaphore; }

private:
    vk::Semaphore m_Semaphore;
    VulkanDevice *m_Device = nullptr;
};
