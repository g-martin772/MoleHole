#include "VulkanAccelerationStructure.h"
#include <spdlog/spdlog.h>

VulkanAccelerationStructure::~VulkanAccelerationStructure() {
    Destroy();
}

void VulkanAccelerationStructure::Destroy() {
    if (m_Device) {
        if (m_Structure) {
            m_Device->GetDevice().destroyAccelerationStructureKHR(m_Structure, nullptr, m_Device->GetDispatcher());
            m_Structure = nullptr;
        }
        m_Buffer.Destroy();
        m_Device = nullptr;
    }
}

void VulkanAccelerationStructure::BuildBLAS(VulkanDevice* device, vk::CommandBuffer cmd, 
                                            const std::vector<BLASInput>& inputs) {
    m_Device = device;

    std::vector<vk::AccelerationStructureGeometryKHR> geometries;
    std::vector<uint32_t> primitiveCounts;

    for (const auto& input : inputs) {
        vk::AccelerationStructureGeometryKHR geometry;
        geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
        geometry.geometry.triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
        geometry.geometry.triangles.vertexData.deviceAddress = input.vertexBuffer->GetDeviceAddress();
        geometry.geometry.triangles.maxVertex = input.vertexCount;
        geometry.geometry.triangles.vertexStride = input.vertexStride;
        geometry.geometry.triangles.indexType = vk::IndexType::eUint32;
        geometry.geometry.triangles.indexData.deviceAddress = input.indexBuffer->GetDeviceAddress();
        geometry.geometry.triangles.transformData.deviceAddress = 0; // Identity transform implied
        geometry.geometry.triangles.pNext = nullptr;
        geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
        
        geometries.push_back(geometry);
        primitiveCounts.push_back(input.indexCount / 3);
    }

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo;
    buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
    buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
    buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
    buildInfo.geometryCount = static_cast<uint32_t>(geometries.size());
    buildInfo.pGeometries = geometries.data();

    // Get size requirements
    vk::AccelerationStructureBuildSizesInfoKHR sizeInfo;
    device->GetDevice().getAccelerationStructureBuildSizesKHR(
        vk::AccelerationStructureBuildTypeKHR::eDevice,
        &buildInfo,
        primitiveCounts.data(),
        &sizeInfo,
        device->GetDispatcher()
    );

    m_Size = sizeInfo.accelerationStructureSize;

    // Allocate AS buffer
    VulkanBufferSpec bufferSpec;
    bufferSpec.Size = m_Size;
    bufferSpec.Usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
    bufferSpec.MemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
    m_Buffer.Create(bufferSpec, device);

    // Create AS handle
    vk::AccelerationStructureCreateInfoKHR createInfo;
    createInfo.buffer = m_Buffer.GetBuffer();
    createInfo.size = m_Size;
    createInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
    
    m_Structure = device->GetDevice().createAccelerationStructureKHR(createInfo, nullptr, device->GetDispatcher());
    
    // Create scratch buffer
    VulkanBuffer* scratchBuffer = CreateScratchBuffer(sizeInfo.buildScratchSize);

    // Build command
    buildInfo.dstAccelerationStructure = m_Structure;
    buildInfo.scratchData.deviceAddress = scratchBuffer->GetDeviceAddress();

    std::vector<vk::AccelerationStructureBuildRangeInfoKHR> rangeInfos;
    for (size_t i = 0; i < inputs.size(); ++i) {
        vk::AccelerationStructureBuildRangeInfoKHR rangeInfo;
        rangeInfo.primitiveCount = primitiveCounts[i];
        rangeInfo.primitiveOffset = 0;
        rangeInfo.firstVertex = 0;
        rangeInfo.transformOffset = 0;
        rangeInfos.push_back(rangeInfo);
    }
    
    const vk::AccelerationStructureBuildRangeInfoKHR* pRangeInfos = rangeInfos.data();
    cmd.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfos, device->GetDispatcher());

    // Add memory barrier
    vk::MemoryBarrier barrier;
    barrier.srcAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR;
    barrier.dstAccessMask = vk::AccessFlagBits::eAccelerationStructureReadKHR;
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
        vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
        vk::DependencyFlagBits::eDeviceGroup,
        1, &barrier, 0, nullptr, 0, nullptr
    );
}

void VulkanAccelerationStructure::BuildBLAS(VulkanDevice* device, vk::CommandBuffer cmd, 
                                            VulkanBuffer* vertexBuffer, VulkanBuffer* indexBuffer, 
                                            uint32_t vertexCount, uint32_t indexCount, vk::DeviceSize vertexStride) {
    BLASInput input;
    input.vertexBuffer = vertexBuffer;
    input.indexBuffer = indexBuffer;
    input.vertexCount = vertexCount;
    input.indexCount = indexCount;
    input.vertexStride = vertexStride;
    BuildBLAS(device, cmd, {input});
}

// Temporary fix: Leak scratch buffer (it will be cleaned up when object is destroyed, which is fine for BLAS created once)
// Wait, if I store it in member, it lives as long as BLAS. That's wasteful but safe.
// Let's add std::vector<VulkanBuffer> m_ScratchBuffers; to class.

void VulkanAccelerationStructure::BuildTLAS(VulkanDevice* device, vk::CommandBuffer cmd, 
                                            VulkanBuffer* instanceBuffer, uint32_t instanceCount, bool update) {
    m_Device = device;

    vk::AccelerationStructureGeometryKHR geometry;
    geometry.geometryType = vk::GeometryTypeKHR::eInstances;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = instanceBuffer->GetDeviceAddress();
    geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo;
    buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
    buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
    buildInfo.mode = update ? vk::BuildAccelerationStructureModeKHR::eUpdate : vk::BuildAccelerationStructureModeKHR::eBuild;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.srcAccelerationStructure = update ? m_Structure : nullptr;

    vk::AccelerationStructureBuildSizesInfoKHR sizeInfo;
    device->GetDevice().getAccelerationStructureBuildSizesKHR(
        vk::AccelerationStructureBuildTypeKHR::eDevice,
        &buildInfo,
        &instanceCount,
        &sizeInfo,
        device->GetDispatcher()
    );

    if (!update) {
        if (m_Structure) {
            device->GetDevice().destroyAccelerationStructureKHR(m_Structure, nullptr, device->GetDispatcher());
            m_Structure = nullptr;
        }
        m_Buffer.Destroy();

        m_Size = sizeInfo.accelerationStructureSize;
        VulkanBufferSpec bufferSpec;
        bufferSpec.Size = m_Size;
        bufferSpec.Usage = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        bufferSpec.MemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        m_Buffer.Create(bufferSpec, device);

        vk::AccelerationStructureCreateInfoKHR createInfo;
        createInfo.buffer = m_Buffer.GetBuffer();
        createInfo.size = m_Size;
        createInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        m_Structure = device->GetDevice().createAccelerationStructureKHR(createInfo, nullptr, device->GetDispatcher());
    }

    VulkanBuffer* scratchBuffer = CreateScratchBuffer(sizeInfo.buildScratchSize);
    
    buildInfo.dstAccelerationStructure = m_Structure;
    buildInfo.scratchData.deviceAddress = scratchBuffer->GetDeviceAddress();

    vk::AccelerationStructureBuildRangeInfoKHR rangeInfo;
    rangeInfo.primitiveCount = instanceCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;

    const vk::AccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
    cmd.buildAccelerationStructuresKHR(1, &buildInfo, &pRangeInfo, device->GetDispatcher());

    // Memory barrier
    vk::MemoryBarrier barrier;
    barrier.srcAccessMask = vk::AccessFlagBits::eAccelerationStructureWriteKHR;
    barrier.dstAccessMask = vk::AccessFlagBits::eAccelerationStructureReadKHR; // Or shader read
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
        vk::PipelineStageFlagBits::eRayTracingShaderKHR,
        vk::DependencyFlagBits::eDeviceGroup,
        1, &barrier, 0, nullptr, 0, nullptr
    );
}

VulkanBuffer* VulkanAccelerationStructure::CreateScratchBuffer(vk::DeviceSize size) {
    auto buffer = std::make_unique<VulkanBuffer>();
    VulkanBufferSpec spec;
    spec.Size = size;
    spec.Usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
    spec.MemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
    buffer->Create(spec, m_Device);
    
    VulkanBuffer* ptr = buffer.get();
    m_ScratchBuffers.push_back(std::move(buffer));
    return ptr;
}

uint64_t VulkanAccelerationStructure::GetDeviceAddress() const {
    if (!m_Device || !m_Structure) return 0;
    vk::AccelerationStructureDeviceAddressInfoKHR info;
    info.accelerationStructure = m_Structure;
    return m_Device->GetDevice().getAccelerationStructureAddressKHR(info, m_Device->GetDispatcher());
}
