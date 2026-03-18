#include "VulkanRayTracingPipeline.h"
#include <spdlog/spdlog.h>
#include <vector>

VulkanRayTracingPipeline::~VulkanRayTracingPipeline() {
    Destroy();
}

void VulkanRayTracingPipeline::Init(VulkanDevice* device) {
    m_Device = device;
}

void VulkanRayTracingPipeline::Destroy() {
    if (m_Device) {
        if (m_Pipeline) {
            m_Device->GetDevice().destroyPipeline(m_Pipeline);
            m_Pipeline = nullptr;
        }
        if (m_PipelineLayout) {
            m_Device->GetDevice().destroyPipelineLayout(m_PipelineLayout);
            m_PipelineLayout = nullptr;
        }
        m_SBTBuffer.Destroy();
        m_Device = nullptr;
    }
}

void VulkanRayTracingPipeline::Bind(vk::CommandBuffer cmd) {
    if (m_Pipeline) {
        cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, m_Pipeline);
    }
}

void VulkanRayTracingPipeline::CreatePipeline(const std::string& rgenPath, const std::string& rmissPath, const std::string& rchitPath, 
                                              const std::vector<vk::DescriptorSetLayout>& layouts) {
    if (!m_Device) return;

    // Load Shaders
    // We use VulkanShader helper to compile/load
    // RayGen
    VulkanShader rgenShader(m_Device, {{ShaderStage::RayGen, rgenPath}});
    // Miss
    VulkanShader rmissShader(m_Device, {{ShaderStage::Miss, rmissPath}});
    // Closest Hit
    VulkanShader rchitShader(m_Device, {{ShaderStage::ClosestHit, rchitPath}});

    if (!rgenShader.HasStage(ShaderStage::RayGen) || 
        !rmissShader.HasStage(ShaderStage::Miss) || 
        !rchitShader.HasStage(ShaderStage::ClosestHit)) {
        spdlog::error("Failed to compile one or more RT shaders");
        return;
    }

    std::vector<vk::PipelineShaderStageCreateInfo> stages;
    stages.push_back(rgenShader.GetStageCreateInfos()[0]);
    stages.push_back(rmissShader.GetStageCreateInfos()[0]);
    stages.push_back(rchitShader.GetStageCreateInfos()[0]);

    // Shader Groups
    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> groups;

    // RayGen Group
    vk::RayTracingShaderGroupCreateInfoKHR rgenGroup;
    rgenGroup.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
    rgenGroup.generalShader = 0; // Index in stages
    rgenGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
    rgenGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    rgenGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
    groups.push_back(rgenGroup);

    // Miss Group
    vk::RayTracingShaderGroupCreateInfoKHR rmissGroup;
    rmissGroup.type = vk::RayTracingShaderGroupTypeKHR::eGeneral;
    rmissGroup.generalShader = 1;
    rmissGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
    rmissGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    rmissGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
    groups.push_back(rmissGroup);

    // Hit Group
    vk::RayTracingShaderGroupCreateInfoKHR hitGroup;
    hitGroup.type = vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
    hitGroup.generalShader = VK_SHADER_UNUSED_KHR;
    hitGroup.closestHitShader = 2;
    hitGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    hitGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
    groups.push_back(hitGroup);

    // Pipeline Layout
    vk::PipelineLayoutCreateInfo layoutInfo;
    layoutInfo.setLayoutCount = layouts.size();
    layoutInfo.pSetLayouts = layouts.data();
    
    // Add push constants if needed (not for now)
    
    m_PipelineLayout = m_Device->GetDevice().createPipelineLayout(layoutInfo);

    // Create Pipeline
    vk::RayTracingPipelineCreateInfoKHR pipelineInfo;
    pipelineInfo.stageCount = stages.size();
    pipelineInfo.pStages = stages.data();
    pipelineInfo.groupCount = groups.size();
    pipelineInfo.pGroups = groups.data();
    pipelineInfo.maxPipelineRayRecursionDepth = 1; // Basic
    pipelineInfo.layout = m_PipelineLayout;

    auto result = m_Device->GetDevice().createRayTracingPipelineKHR(nullptr, nullptr, pipelineInfo, nullptr, m_Device->GetDispatcher());
    if (result.result != vk::Result::eSuccess) {
        spdlog::error("Failed to create Ray Tracing Pipeline");
        return;
    }
    m_Pipeline = result.value;
    
    CreateSBT(groups);
    spdlog::info("Ray Tracing Pipeline Created Successfully");
}

uint32_t AlignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

void VulkanRayTracingPipeline::CreateSBT(const std::vector<vk::RayTracingShaderGroupCreateInfoKHR>& groups) {
    // Get RT Properties for alignment
    vk::PhysicalDeviceRayTracingPipelinePropertiesKHR rtProps;
    vk::PhysicalDeviceProperties2 props2;
    props2.pNext = &rtProps;
    m_Device->GetPhysicalDevice().getProperties2(&props2);

    uint32_t handleSize = rtProps.shaderGroupHandleSize;
    uint32_t handleAlignment = rtProps.shaderGroupHandleAlignment;
    uint32_t baseAlignment = rtProps.shaderGroupBaseAlignment;

    spdlog::info("RT Props: HandleSize={}, HandleAlign={}, BaseAlign={}", handleSize, handleAlignment, baseAlignment);

    // Calculate stride and size for each group
    // Each group gets one entry in SBT for simplicity
    uint32_t handleSizeAligned = AlignUp(handleSize, handleAlignment);

    // RayGen
    m_RayGenRegion.stride = AlignUp(handleSizeAligned, baseAlignment);
    m_RayGenRegion.size = m_RayGenRegion.stride; // 1 shader

    // Miss
    m_MissRegion.stride = AlignUp(handleSizeAligned, baseAlignment);
    m_MissRegion.size = m_MissRegion.stride; // 1 shader

    // Hit
    m_HitRegion.stride = AlignUp(handleSizeAligned, baseAlignment);
    m_HitRegion.size = m_HitRegion.stride; // 1 shader (for now)

    // Call
    m_CallRegion.size = 0;

    // Fetch handles
    uint32_t groupCount = groups.size();
    uint32_t sbtSize = groupCount * handleSize;
    std::vector<uint8_t> shaderHandleStorage(sbtSize);
    
    if (m_Device->GetDevice().getRayTracingShaderGroupHandlesKHR(m_Pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data(), m_Device->GetDispatcher()) != vk::Result::eSuccess) {
        spdlog::error("Failed to get shader group handles");
        return;
    }

    // Allocate SBT Buffer
    // Total size needs to cover all regions with their alignments
    vk::DeviceSize bufferSize = m_RayGenRegion.size + m_MissRegion.size + m_HitRegion.size;
    
    // Pad buffer to avoid validation errors if it thinks it's too small
    bufferSize = std::max(bufferSize, (vk::DeviceSize)1024);

    spdlog::info("SBT Buffer Size: {}, RGen: {}, Miss: {}, Hit: {}", bufferSize, m_RayGenRegion.size, m_MissRegion.size, m_HitRegion.size);

    VulkanBufferSpec spec;
    spec.Size = bufferSize;
    spec.Usage = vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst; // TransferDst for copy if needed, but we map
    spec.MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    m_SBTBuffer.Create(spec, m_Device);

    // Write handles to buffer
    uint8_t* mappedData = static_cast<uint8_t*>(m_SBTBuffer.Map());
    if (!mappedData) return;

    vk::DeviceAddress sbtAddress = m_SBTBuffer.GetDeviceAddress();
    spdlog::info("SBT Buffer Address: {0:x}", sbtAddress);

    // RayGen (Group 0)
    memcpy(mappedData, shaderHandleStorage.data() + 0 * handleSize, handleSize);
    m_RayGenRegion.deviceAddress = sbtAddress;
    
    // Miss (Group 1)
    uint8_t* missPtr = mappedData + m_RayGenRegion.size;
    memcpy(missPtr, shaderHandleStorage.data() + 1 * handleSize, handleSize);
    m_MissRegion.deviceAddress = sbtAddress + m_RayGenRegion.size;

    // Hit (Group 2)
    uint8_t* hitPtr = missPtr + m_MissRegion.size;
    memcpy(hitPtr, shaderHandleStorage.data() + 2 * handleSize, handleSize);
    m_HitRegion.deviceAddress = sbtAddress + m_RayGenRegion.size + m_MissRegion.size;

    spdlog::info("SBT Regions: RGen={0:x}, Miss={1:x}, Hit={2:x}", m_RayGenRegion.deviceAddress, m_MissRegion.deviceAddress, m_HitRegion.deviceAddress);

    m_SBTBuffer.Unmap();
}
