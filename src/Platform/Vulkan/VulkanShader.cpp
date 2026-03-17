#include "VulkanShader.h"

#include <ranges>

#include "VulkanDevice.h"
#include "ShaderUtils.h"
#include <shaderc/shaderc.hpp>

static constexpr const char* k_CacheSubDir = "vulkan";
static constexpr const char* k_CacheExt    = ".spv";

static shaderc_shader_kind StageToShadercKind(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:                 return shaderc_vertex_shader;
        case ShaderStage::TessellationControl:    return shaderc_tess_control_shader;
        case ShaderStage::TessellationEvaluation: return shaderc_tess_evaluation_shader;
        case ShaderStage::Geometry:               return shaderc_geometry_shader;
        case ShaderStage::Fragment:               return shaderc_fragment_shader;
        case ShaderStage::Compute:                return shaderc_compute_shader;
        case ShaderStage::RayGen:                 return shaderc_raygen_shader;
        case ShaderStage::AnyHit:                 return shaderc_anyhit_shader;
        case ShaderStage::ClosestHit:             return shaderc_closesthit_shader;
        case ShaderStage::Miss:                   return shaderc_miss_shader;
        case ShaderStage::Intersection:           return shaderc_intersection_shader;
        case ShaderStage::Callable:               return shaderc_callable_shader;
    }
    return shaderc_glsl_infer_from_source;
}

static const char* StageName(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:                 return "Vertex";
        case ShaderStage::TessellationControl:    return "TessControl";
        case ShaderStage::TessellationEvaluation: return "TessEval";
        case ShaderStage::Geometry:               return "Geometry";
        case ShaderStage::Fragment:               return "Fragment";
        case ShaderStage::Compute:                return "Compute";
        case ShaderStage::RayGen:                 return "RayGen";
        case ShaderStage::AnyHit:                 return "AnyHit";
        case ShaderStage::ClosestHit:             return "ClosestHit";
        case ShaderStage::Miss:                   return "Miss";
        case ShaderStage::Intersection:           return "Intersection";
        case ShaderStage::Callable:               return "Callable";
    }
    return "Unknown";
}

VulkanShader::VulkanShader(VulkanDevice* device,
                           std::initializer_list<std::pair<ShaderStage, std::string>> stages)
    : m_Device(device)
{
    Compile({stages.begin(), stages.end()});
}

VulkanShader::VulkanShader(VulkanDevice* device,
                           const std::vector<std::pair<ShaderStage, std::string>>& stages)
    : m_Device(device)
{
    Compile(stages);
}

VulkanShader::~VulkanShader() {
    if (!m_Device) return;
    const vk::Device dev = m_Device->GetDevice();
    for (auto &module: m_StageModules | std::views::values) {
        if (module)
            dev.destroyShaderModule(module);
    }
}

VulkanShader::VulkanShader(VulkanShader&& other) noexcept
    : m_Device(other.m_Device)
    , m_StageModules(std::move(other.m_StageModules))
{
    other.m_Device = nullptr;
}

VulkanShader& VulkanShader::operator=(VulkanShader&& other) noexcept {
    if (this != &other) {
        if (m_Device) {
            const vk::Device dev = m_Device->GetDevice();
            for (auto &module: m_StageModules | std::views::values)
                if (module) dev.destroyShaderModule(module);
        }
        m_Device       = other.m_Device;
        m_StageModules = std::move(other.m_StageModules);
        other.m_Device = nullptr;
    }
    return *this;
}

void VulkanShader::Compile(const std::vector<std::pair<ShaderStage, std::string>>& stages) {
    for (const auto& [stage, path] : stages) {
        if (const vk::ShaderModule module = CompileStage(stage, path))
            m_StageModules[stage] = module;
    }
}

vk::ShaderModule VulkanShader::CompileStage(ShaderStage stage, const std::string& path) {
    const std::string cacheKey = ShaderUtils::ComputeHash(path + "|" + StageName(stage));

    if (ShaderUtils::IsCacheValid(cacheKey, path.c_str(), nullptr, k_CacheSubDir, k_CacheExt)) {
        std::vector<uint32_t> spirv;
        if (ShaderUtils::LoadBinaryCache(cacheKey, spirv, k_CacheSubDir, k_CacheExt)) {
            vk::ShaderModuleCreateInfo ci({}, spirv.size() * sizeof(uint32_t), spirv.data());
            try {
                const vk::ShaderModule module = m_Device->GetDevice().createShaderModule(ci);
                spdlog::info("[VulkanShader] Loaded {} stage from cache: {}", StageName(stage), path);
                return module;
            } catch (const vk::SystemError& e) {
                spdlog::warn("[VulkanShader] createShaderModule from cache failed ({}), recompiling: {}", e.what(), path);
            }
        }
    }

    const std::string source = ShaderUtils::ReadFile(path.c_str());
    if (source.empty()) {
        spdlog::error("[VulkanShader] Empty source for {} stage: {}", StageName(stage), path);
        return VK_NULL_HANDLE;
    }

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetGenerateDebugInfo();

    const auto tStart = std::chrono::steady_clock::now();
    const shaderc::SpvCompilationResult result =
        compiler.CompileGlslToSpv(source, StageToShadercKind(stage), path.c_str(), options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        spdlog::error("[VulkanShader] {} stage compilation failed:\n{}", StageName(stage), result.GetErrorMessage());
        return VK_NULL_HANDLE;
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - tStart).count();
    spdlog::info("[VulkanShader] Compiled {} stage in {} ms: {}", StageName(stage), ms, path);

    const std::vector spirv(result.cbegin(), result.cend());

    ShaderUtils::SaveBinaryCache(cacheKey,
                                 spirv.data(), spirv.size() * sizeof(uint32_t),
                                 ShaderUtils::GetFileModTime(path.c_str()),
                                 0,
                                 k_CacheSubDir, k_CacheExt);

    vk::ShaderModuleCreateInfo ci({}, spirv.size() * sizeof(uint32_t), spirv.data());
    try {
        return m_Device->GetDevice().createShaderModule(ci);
    } catch (const vk::SystemError& e) {
        spdlog::error("[VulkanShader] createShaderModule failed: {}", e.what());
        return VK_NULL_HANDLE;
    }
}

vk::ShaderStageFlagBits VulkanShader::ToVkStage(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex:                 return vk::ShaderStageFlagBits::eVertex;
        case ShaderStage::TessellationControl:    return vk::ShaderStageFlagBits::eTessellationControl;
        case ShaderStage::TessellationEvaluation: return vk::ShaderStageFlagBits::eTessellationEvaluation;
        case ShaderStage::Geometry:               return vk::ShaderStageFlagBits::eGeometry;
        case ShaderStage::Fragment:               return vk::ShaderStageFlagBits::eFragment;
        case ShaderStage::Compute:                return vk::ShaderStageFlagBits::eCompute;
        case ShaderStage::RayGen:                 return vk::ShaderStageFlagBits::eRaygenKHR;
        case ShaderStage::AnyHit:                 return vk::ShaderStageFlagBits::eAnyHitKHR;
        case ShaderStage::ClosestHit:             return vk::ShaderStageFlagBits::eClosestHitKHR;
        case ShaderStage::Miss:                   return vk::ShaderStageFlagBits::eMissKHR;
        case ShaderStage::Intersection:           return vk::ShaderStageFlagBits::eIntersectionKHR;
        case ShaderStage::Callable:               return vk::ShaderStageFlagBits::eCallableKHR;
    }
    return vk::ShaderStageFlagBits::eAll;
}

std::vector<vk::PipelineShaderStageCreateInfo> VulkanShader::GetStageCreateInfos() const {
    std::vector<vk::PipelineShaderStageCreateInfo> infos;
    infos.reserve(m_StageModules.size());
    for (const auto& [stage, module] : m_StageModules) {
        infos.emplace_back(vk::PipelineShaderStageCreateFlags{},
                           ToVkStage(stage),
                           module,
                           k_EntryPoint);
    }
    return infos;
}

vk::ShaderModule VulkanShader::GetModule(ShaderStage stage) const {
    if (const auto it = m_StageModules.find(stage); it != m_StageModules.end())
        return it->second;
    return VK_NULL_HANDLE;
}

bool VulkanShader::HasStage(ShaderStage stage) const {
    return m_StageModules.contains(stage);
}

void VulkanShader::Bind(vk::CommandBuffer cmd,
                        vk::Pipeline pipeline,
                        vk::PipelineBindPoint bindPoint) const {
    cmd.bindPipeline(bindPoint, pipeline);
}

void VulkanShader::PushConstants(vk::CommandBuffer cmd,
                                 vk::PipelineLayout layout,
                                 vk::ShaderStageFlags stageFlags,
                                 uint32_t offset,
                                 uint32_t size,
                                 const void* data) const {
    cmd.pushConstants(layout, stageFlags, offset, size, data);
}

void VulkanShader::BindDescriptorSet(vk::CommandBuffer cmd,
                                     vk::PipelineLayout layout,
                                     uint32_t setIndex,
                                     vk::DescriptorSet descriptorSet,
                                     vk::PipelineBindPoint bindPoint) const {
    cmd.bindDescriptorSets(bindPoint, layout, setIndex, {descriptorSet}, {});
}
