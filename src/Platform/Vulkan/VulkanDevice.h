#pragma once

#include <vulkan/vulkan.hpp>

class VulkanInstance;

struct DeviceRequirements {
    bool Graphics, Compute, Transfer, Sparse, Present;
    vk::SurfaceKHR Surface;
};

struct VulkanQueueIndices {
    uint32_t Graphics = -1, Compute = -1, Transfer = -1, Sparse = -1, Present = -1;
};

class VulkanDevice {
public:
    void Init(const DeviceRequirements &requirements, VulkanInstance *instance);
    void Shutdown();
    void WaitIdle();

    vk::Device GetDevice() const { return m_Device; }
    vk::PhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
    DeviceRequirements GetRequirements() const { return m_Requirements; }
    VulkanQueueIndices GetQueueIndices() const { return m_QueueIndices; }
    vk::Format GetDepthFormat() const { return m_DepthFormat; }
    vk::SurfaceCapabilitiesKHR GetSurfaceCapabilities() const { return m_SurfaceCapabilities; }
    std::vector<vk::SurfaceFormatKHR> GetSurfaceFormats() const { return m_SurfaceFormats; }
    std::vector<vk::PresentModeKHR> GetSurfacePresentModes() const { return m_SurfacePresentModes; }

    vk::Queue GetGraphicsQueue() const { return m_Queues[m_GraphicsIndex]; }
    vk::Queue GetTransferQueue() const { return m_Queues[m_TransferIndex]; }
    vk::Queue GetComputeQueue() const { return m_Queues[m_ComputeIndex]; }
    vk::Queue GetSparseQueue() const { return m_Queues[m_SparseIndex]; }
    vk::Queue GetPresentQueue() const { return m_Queues[m_PresentIndex]; }

    const vk::detail::DispatchLoaderDynamic& GetDispatcher() const { return m_Dispatcher; }

private:
    DeviceRequirements m_Requirements = {};
    VulkanInstance *m_Instance = nullptr;
    vk::Device m_Device;
    vk::PhysicalDevice m_PhysicalDevice;
    vk::detail::DispatchLoaderDynamic m_Dispatcher;
    VulkanQueueIndices m_QueueIndices;
    std::vector<vk::Queue> m_Queues;
    uint32_t m_GraphicsIndex = 0, m_TransferIndex = 0, m_ComputeIndex = 0,
            m_SparseIndex = 0, m_PresentIndex = 0;
    vk::Format m_DepthFormat = vk::Format::eD24UnormS8Uint;
    vk::SurfaceCapabilitiesKHR m_SurfaceCapabilities;
    std::vector<vk::SurfaceFormatKHR> m_SurfaceFormats;
    std::vector<vk::PresentModeKHR> m_SurfacePresentModes;
};
