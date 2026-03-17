#pragma once

#include <vulkan/vulkan.hpp>

class VulkanDevice;

enum class ShaderStage : uint32_t {
    Vertex                 = 0,
    TessellationControl    = 1,
    TessellationEvaluation = 2,
    Geometry               = 3,
    Fragment               = 4,
    Compute                = 5,
    RayGen                 = 6,
    AnyHit                 = 7,
    ClosestHit             = 8,
    Miss                   = 9,
    Intersection           = 10,
    Callable               = 11,
};

// A compiled, cache-backed Vulkan shader.
// Each stage is compiled independently from GLSL → SPIR-V via shaderc,
// with results cached under .shader_cache/vulkan/ keyed by file timestamp.
//
// Usage:
//   VulkanShader shader(device, {
//       { ShaderStage::Vertex,   "shaders/mesh.vert" },
//       { ShaderStage::Fragment, "shaders/mesh.frag" },
//   });
//   auto stageInfos = shader.GetStageCreateInfos();
//   // ... build vk::GraphicsPipelineCreateInfo using stageInfos
//   // For push constants:
//   shader.PushConstants(cmd, layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(data), &data);
class VulkanShader {
public:
    VulkanShader(VulkanDevice* device,
                 std::initializer_list<std::pair<ShaderStage, std::string>> stages);

    VulkanShader(VulkanDevice* device,
                 const std::vector<std::pair<ShaderStage, std::string>>& stages);

    ~VulkanShader();

    VulkanShader(const VulkanShader&)            = delete;
    VulkanShader& operator=(const VulkanShader&) = delete;
    VulkanShader(VulkanShader&&) noexcept;
    VulkanShader& operator=(VulkanShader&&) noexcept;

    std::vector<vk::PipelineShaderStageCreateInfo> GetStageCreateInfos() const;

    vk::ShaderModule GetModule(ShaderStage stage) const;
    bool HasStage(ShaderStage stage) const;

    void Bind(vk::CommandBuffer cmd,
              vk::Pipeline pipeline,
              vk::PipelineBindPoint bindPoint = vk::PipelineBindPoint::eGraphics) const;

    void PushConstants(vk::CommandBuffer cmd,
                       vk::PipelineLayout layout,
                       vk::ShaderStageFlags stageFlags,
                       uint32_t offset,
                       uint32_t size,
                       const void* data) const;

    void BindDescriptorSet(vk::CommandBuffer cmd,
                           vk::PipelineLayout layout,
                           uint32_t setIndex,
                           vk::DescriptorSet descriptorSet,
                           vk::PipelineBindPoint bindPoint = vk::PipelineBindPoint::eGraphics) const;

    static vk::ShaderStageFlagBits ToVkStage(ShaderStage stage);
private:
    void Compile(const std::vector<std::pair<ShaderStage, std::string>>& stages);
    vk::ShaderModule CompileStage(ShaderStage stage, const std::string& path);

    VulkanDevice* m_Device = nullptr;
    std::unordered_map<ShaderStage, vk::ShaderModule> m_StageModules;

    static constexpr const char* k_EntryPoint = "main";
};
