#pragma once
#include <vulkan/vulkan.hpp>
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanShader.h"

class VulkanRayTracingPipeline {
public:
    VulkanRayTracingPipeline() = default;
    ~VulkanRayTracingPipeline();

    void Init(VulkanDevice* device);
    void Destroy();

    /**
     * Create the ray tracing pipeline
     * @param rgenPath Path to ray generation shader (.rgen)
     * @param rmissPath Path to miss shader (.rmiss)
     * @param rchitPath Path to closest hit shader (.rchit)
     * @param layouts Descriptor set layouts
     */
    void CreatePipeline(const std::string& rgenPath, const std::string& rmissPath, const std::string& rchitPath, 
                        const std::vector<vk::DescriptorSetLayout>& layouts);

    void Bind(vk::CommandBuffer cmd);
    
    // Getters for SBT regions (needed for traceRays call)
    const vk::StridedDeviceAddressRegionKHR& GetRayGenRegion() const { return m_RayGenRegion; }
    const vk::StridedDeviceAddressRegionKHR& GetMissRegion() const { return m_MissRegion; }
    const vk::StridedDeviceAddressRegionKHR& GetHitRegion() const { return m_HitRegion; }
    const vk::StridedDeviceAddressRegionKHR& GetCallRegion() const { return m_CallRegion; }

    vk::Pipeline GetPipeline() const { return m_Pipeline; }
    vk::PipelineLayout GetLayout() const { return m_PipelineLayout; }

private:
    VulkanDevice* m_Device = nullptr;
    vk::Pipeline m_Pipeline;
    vk::PipelineLayout m_PipelineLayout;

    // SBT Buffers
    VulkanBuffer m_SBTBuffer;
    vk::StridedDeviceAddressRegionKHR m_RayGenRegion{};
    vk::StridedDeviceAddressRegionKHR m_MissRegion{};
    vk::StridedDeviceAddressRegionKHR m_HitRegion{};
    vk::StridedDeviceAddressRegionKHR m_CallRegion{};

    void CreateSBT(const std::vector<vk::RayTracingShaderGroupCreateInfoKHR>& groups);
    
    // Helper to align sizes
    uint32_t AlignUp(uint32_t size, uint32_t alignment) {
        return (size + alignment - 1) & ~(alignment - 1);
    }
};
