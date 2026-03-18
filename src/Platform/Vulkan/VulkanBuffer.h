#pragma once

#include <vulkan/vulkan.hpp>
#include <cstring>

class VulkanDevice;

struct VulkanBufferSpec {
    vk::DeviceSize Size;
    vk::BufferUsageFlags Usage;
    vk::MemoryPropertyFlags MemoryProperties;
};

class VulkanBuffer {
public:
    VulkanBuffer() = default;
    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    VulkanBuffer(VulkanBuffer&&) = default;
    VulkanBuffer& operator=(VulkanBuffer&&) = default;

    /**
     * Create and allocate a Vulkan buffer
     * @param spec Buffer specification (size, usage, memory properties)
     * @param device The Vulkan device
     */
    void Create(const VulkanBufferSpec &spec, VulkanDevice *device);

    /**
     * Destroy and deallocate the buffer and associated memory
     */
    void Destroy();

    /**
     * Map the buffer memory to host-accessible memory
     * @return Pointer to mapped memory
     */
    void *Map();

    /**
     * Unmap the previously mapped buffer memory
     */
    void Unmap();

    /**
     * Write data to the buffer
     * For device-local buffers, this uses a staging buffer
     * For host-visible buffers, this maps directly
     * @param data Pointer to data to write
     * @param size Size of data to write in bytes
     * @param offset Offset in the buffer to write to
     */
    void Write(const void *data, vk::DeviceSize size, vk::DeviceSize offset = 0);

    /**
     * Read data from the buffer
     * Only works for host-visible buffers
     * @param data Pointer to buffer to read into
     * @param size Size of data to read in bytes
     * @param offset Offset in the buffer to read from
     */
    void Read(void *data, vk::DeviceSize size, vk::DeviceSize offset = 0);

    /**
     * Get the underlying Vulkan buffer handle
     */
    vk::Buffer GetBuffer() const { return m_Buffer; }

    /**
     * Get the size of the buffer in bytes
     */
    vk::DeviceSize GetSize() const { return m_Size; }

    /**
     * Get the memory properties of this buffer
     */
    vk::MemoryPropertyFlags GetMemoryProperties() const { return m_MemoryProperties; }

    /**
     * Get the usage flags of this buffer
     */
    vk::BufferUsageFlags GetUsageFlags() const { return m_Usage; }

    /**
     * Get descriptor info for use in descriptor sets (for uniform/storage buffers)
     */
    vk::DescriptorBufferInfo GetDescriptorInfo() const {
        return vk::DescriptorBufferInfo{m_Buffer, 0, m_Size};
    }

    /**
     * Get descriptor info for a specific range
     */
    vk::DescriptorBufferInfo GetDescriptorInfo(vk::DeviceSize offset, vk::DeviceSize range) const {
        return vk::DescriptorBufferInfo{m_Buffer, offset, range};
    }

    /**
     * Check if the buffer is host-visible (can be mapped to CPU memory)
     */
    bool IsHostVisible() const {
        return (m_MemoryProperties & vk::MemoryPropertyFlagBits::eHostVisible) == vk::MemoryPropertyFlagBits::eHostVisible;
    }

    /**
     * Check if the buffer is device-local
     */
    bool IsDeviceLocal() const {
        return (m_MemoryProperties & vk::MemoryPropertyFlagBits::eDeviceLocal) == vk::MemoryPropertyFlagBits::eDeviceLocal;
    }

    /**
     * Get the device address of the buffer (required for Ray Tracing)
     */
    vk::DeviceAddress GetDeviceAddress() const;

private:
    VulkanDevice *m_Device = nullptr;
    vk::Buffer m_Buffer;
    vk::DeviceMemory m_Memory;
    vk::DeviceSize m_Size = 0;
    vk::BufferUsageFlags m_Usage;
    vk::MemoryPropertyFlags m_MemoryProperties;
    void *m_MappedPtr = nullptr;
    bool m_IsMapped = false;

    /**
     * Copy data between buffers using command buffer
     * Used for staging buffer operations
     */
    void CopyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size);

    /**
     * Create a staging buffer and copy data to it, then transfer to the target buffer
     */
    void WriteViaStagingBuffer(const void *data, vk::DeviceSize size, vk::DeviceSize offset);
};
