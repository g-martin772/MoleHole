#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "VulkanMemory.h"
#include "VulkanCommandBuffer.h"
#include <spdlog/spdlog.h>

VulkanBuffer::~VulkanBuffer() {
    if (m_Device != nullptr) {
        Destroy();
    }
}

void VulkanBuffer::Create(const VulkanBufferSpec &spec, VulkanDevice *device) {
    m_Device = device;
    m_Size = spec.Size;
    m_Usage = spec.Usage;
    m_MemoryProperties = spec.MemoryProperties;

    // Create the buffer
    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = spec.Size;
    bufferInfo.usage = spec.Usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    // spdlog::info("Creating Buffer: Size={}, Usage={}", spec.Size, vk::to_string(spec.Usage));

    try {
        m_Buffer = m_Device->GetDevice().createBuffer(bufferInfo);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to create Vulkan Buffer: {}", err.what());
        return;
    }
    // spdlog::trace("Created Vulkan Buffer: successful");

    // Get memory requirements
    vk::MemoryRequirements requirements = m_Device->GetDevice().getBufferMemoryRequirements(m_Buffer);
    // spdlog::info("Buffer Memory Requirements: Size={}, Alignment={}, TypeBits={}", requirements.size, requirements.alignment, requirements.memoryTypeBits);

    // Find suitable memory type
    const uint32_t memoryIndex = VulkanMemory::FindMemoryIndex(m_Device, requirements.memoryTypeBits,
                                                                spec.MemoryProperties);

    if (memoryIndex == static_cast<uint32_t>(-1)) {
        spdlog::error("Failed to find suitable memory type for buffer");
        return;
    }

    // Allocate memory
    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = memoryIndex;

    // Enable device address allocation if requested
    vk::MemoryAllocateFlagsInfo allocFlagsInfo{};
    if (spec.Usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
        allocFlagsInfo.flags = vk::MemoryAllocateFlagBits::eDeviceAddress;
        allocInfo.pNext = &allocFlagsInfo;
    }

    try {
        m_Memory = m_Device->GetDevice().allocateMemory(allocInfo);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to allocate Vulkan buffer memory: {}", err.what());
        return;
    }

    // Bind memory to buffer
    try {
        m_Device->GetDevice().bindBufferMemory(m_Buffer, m_Memory, 0);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to bind buffer memory: {}", err.what());
        return;
    }

    spdlog::trace("Vulkan Buffer successfully allocated and bound");
}

void VulkanBuffer::Destroy() {
    if (!m_Device) return;

    if (m_IsMapped) {
        Unmap();
    }

    if (m_Buffer) {
        m_Device->GetDevice().destroyBuffer(m_Buffer);
        m_Buffer = nullptr;
    }

    if (m_Memory) {
        m_Device->GetDevice().freeMemory(m_Memory);
        m_Memory = nullptr;
    }

    spdlog::trace("Vulkan Buffer destroyed");
}

void *VulkanBuffer::Map() {
    if (!m_Device || !m_Memory) {
        spdlog::error("Cannot map buffer: device or memory is null");
        return nullptr;
    }

    if (m_IsMapped) {
        spdlog::warn("Buffer is already mapped");
        return m_MappedPtr;
    }

    if (!IsHostVisible()) {
        spdlog::error("Cannot map device-local buffer without host-visible property");
        return nullptr;
    }

    try {
        m_MappedPtr = m_Device->GetDevice().mapMemory(m_Memory, 0, m_Size, {});
        m_IsMapped = true;
        spdlog::trace("Buffer mapped successfully");
        return m_MappedPtr;
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to map buffer memory: {}", err.what());
        return nullptr;
    }
}

void VulkanBuffer::Unmap() {
    if (!m_Device || !m_Memory || !m_IsMapped) {
        return;
    }

    m_Device->GetDevice().unmapMemory(m_Memory);
    m_MappedPtr = nullptr;
    m_IsMapped = false;
    spdlog::trace("Buffer unmapped successfully");
}

void VulkanBuffer::Write(const void *data, vk::DeviceSize size, vk::DeviceSize offset) {
    if (!m_Device || !data || size == 0) {
        spdlog::error("Invalid parameters for buffer write");
        return;
    }

    if (offset + size > m_Size) {
        spdlog::error("Write would exceed buffer bounds");
        return;
    }

    // If buffer is host-visible and coherent, write directly
    if (IsHostVisible()) {
        void *mappedPtr = Map();
        if (mappedPtr) {
            std::memcpy(static_cast<char *>(mappedPtr) + offset, data, size);

            // Flush memory if not host-coherent
            if ((m_MemoryProperties & vk::MemoryPropertyFlagBits::eHostCoherent) !=
                vk::MemoryPropertyFlagBits::eHostCoherent) {
                vk::MappedMemoryRange range{};
                range.memory = m_Memory;
                range.offset = offset;
                range.size = size;
                m_Device->GetDevice().flushMappedMemoryRanges(range);
            }

            Unmap();
        }
    } else {
        // Use staging buffer for device-local buffers
        WriteViaStagingBuffer(data, size, offset);
    }

    spdlog::trace("Buffer write completed: {} bytes at offset {}", size, offset);
}

void VulkanBuffer::Read(void *data, vk::DeviceSize size, vk::DeviceSize offset) {
    if (!m_Device || !data || size == 0) {
        spdlog::error("Invalid parameters for buffer read");
        return;
    }

    if (offset + size > m_Size) {
        spdlog::error("Read would exceed buffer bounds");
        return;
    }

    if (!IsHostVisible()) {
        spdlog::error("Cannot read from device-local buffer");
        return;
    }

    void *mappedPtr = Map();
    if (mappedPtr) {
        // Invalidate memory if not host-coherent
        if ((m_MemoryProperties & vk::MemoryPropertyFlagBits::eHostCoherent) !=
            vk::MemoryPropertyFlagBits::eHostCoherent) {
            vk::MappedMemoryRange range{};
            range.memory = m_Memory;
            range.offset = offset;
            range.size = size;
            m_Device->GetDevice().invalidateMappedMemoryRanges(range);
        }

        std::memcpy(data, static_cast<char *>(mappedPtr) + offset, size);
        Unmap();
    }

    spdlog::trace("Buffer read completed: {} bytes from offset {}", size, offset);
}

void VulkanBuffer::CopyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size) {
    // Create a temporary command buffer for the copy operation
    VulkanCommandPool commandPool;
    commandPool.Init(m_Device, m_Device->GetQueueIndices().Transfer);

    auto cmdBuffer = commandPool.AllocateSingleUseCommandBuffer();
    if (!cmdBuffer) {
        spdlog::error("Failed to allocate command buffer for buffer copy");
        return;
    }

    cmdBuffer->Begin();

    vk::BufferCopy copyRegion{};
    copyRegion.size = size;
    cmdBuffer->GetCommandBuffer().copyBuffer(srcBuffer, dstBuffer, copyRegion);

    cmdBuffer->End();
    cmdBuffer->Submit(m_Device->GetTransferQueue());

    // Wait for transfer to complete
    m_Device->WaitIdle();

    commandPool.Destroy();
}

void VulkanBuffer::WriteViaStagingBuffer(const void *data, vk::DeviceSize size, vk::DeviceSize offset) {
    // Create staging buffer
    VulkanBufferSpec stagingSpec{};
    stagingSpec.Size = size;
    stagingSpec.Usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingSpec.MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;

    VulkanBuffer stagingBuffer;
    stagingBuffer.Create(stagingSpec, m_Device);

    // Write data to staging buffer
    void *stagingPtr = stagingBuffer.Map();
    if (stagingPtr) {
        std::memcpy(stagingPtr, data, size);
        stagingBuffer.Unmap();
    }

    // Copy from staging buffer to device-local buffer
    VulkanCommandPool commandPool;
    commandPool.Init(m_Device, m_Device->GetQueueIndices().Transfer);

    auto cmdBuffer = commandPool.AllocateSingleUseCommandBuffer();
    if (cmdBuffer) {
        cmdBuffer->Begin();

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = offset;
        copyRegion.size = size;
        cmdBuffer->GetCommandBuffer().copyBuffer(stagingBuffer.GetBuffer(), m_Buffer, copyRegion);

        cmdBuffer->End();
        cmdBuffer->Submit(m_Device->GetTransferQueue());

        // Wait for transfer to complete
        m_Device->WaitIdle();

        commandPool.Destroy();
    }

    stagingBuffer.Destroy();
}

vk::DeviceAddress VulkanBuffer::GetDeviceAddress() const {
    if (!m_Device || !m_Buffer) return 0;
    
    vk::BufferDeviceAddressInfo info{};
    info.buffer = m_Buffer;
    return m_Device->GetDevice().getBufferAddress(info);
}
