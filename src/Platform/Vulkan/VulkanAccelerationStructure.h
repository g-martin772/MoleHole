#pragma once

#include <vector>
#include <vulkan/vulkan.hpp>
#include "VulkanDevice.h"
#include "VulkanBuffer.h"

struct BLASInput {
    VulkanBuffer* vertexBuffer;
    VulkanBuffer* indexBuffer;
    uint32_t vertexCount;
    uint32_t indexCount;
    vk::DeviceSize vertexStride;
    uint32_t transformOffset = 0; // if using transform buffer
};

class VulkanAccelerationStructure {
public:
    VulkanAccelerationStructure() = default;
    ~VulkanAccelerationStructure();

    /**
     * Build Bottom Level Acceleration Structure (BLAS)
     * @param device Vulkan Device
     * @param cmd Command Buffer (must be in recording state)
     * @param inputs List of geometries to include in BLAS
     */
    void BuildBLAS(VulkanDevice* device, vk::CommandBuffer cmd, const std::vector<BLASInput>& inputs);

    /**
     * Build Bottom Level Acceleration Structure (BLAS) - Single Geometry Convenience
     */
    void BuildBLAS(VulkanDevice* device, vk::CommandBuffer cmd, 
                   VulkanBuffer* vertexBuffer, VulkanBuffer* indexBuffer, 
                   uint32_t vertexCount, uint32_t indexCount, vk::DeviceSize vertexStride);

    /**
     * Build Top Level Acceleration Structure (TLAS)
     * @param device Vulkan Device
     * @param cmd Command Buffer (must be in recording state)
     * @param instanceBuffer Buffer containing VkAccelerationStructureInstanceKHR data
     * @param instanceCount Number of instances
     * @param update Whether this is an update operation (not implemented yet)
     */
    void BuildTLAS(VulkanDevice* device, vk::CommandBuffer cmd, 
                   VulkanBuffer* instanceBuffer, uint32_t instanceCount, bool update = false);

    void Destroy();

    vk::AccelerationStructureKHR GetHandle() const { return m_Structure; }
    uint64_t GetDeviceAddress() const;

private:
    VulkanDevice* m_Device = nullptr;
    vk::AccelerationStructureKHR m_Structure;
    VulkanBuffer m_Buffer;
    vk::DeviceSize m_Size = 0;
    
    // Helper to allocate scratch buffer
    VulkanBuffer* CreateScratchBuffer(vk::DeviceSize size);
    
    // Store scratch buffers to keep them alive during command execution
    std::vector<std::unique_ptr<VulkanBuffer>> m_ScratchBuffers;
};
