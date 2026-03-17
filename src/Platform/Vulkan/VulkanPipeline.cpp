#include "VulkanPipeline.h"

#include "VulkanDevice.h"
#include "VulkanShader.h"
#include "VulkanRenderPass.h"
#include <spdlog/spdlog.h>

// ===========================================================================================
// Constructor and Destructor
// ===========================================================================================

VulkanPipeline::~VulkanPipeline() {
    Destroy();
}

VulkanPipeline::VulkanPipeline(VulkanPipeline&& other) noexcept
    : m_Device(other.m_Device)
    , m_Pipeline(other.m_Pipeline)
    , m_PipelineLayout(other.m_PipelineLayout)
    , m_DescriptorSetLayouts(std::move(other.m_DescriptorSetLayouts))
    , m_IsGraphics(other.m_IsGraphics)
{
    other.m_Device = nullptr;
    other.m_Pipeline = VK_NULL_HANDLE;
    other.m_PipelineLayout = VK_NULL_HANDLE;
}

VulkanPipeline& VulkanPipeline::operator=(VulkanPipeline&& other) noexcept {
    if (this != &other) {
        Destroy();
        m_Device = other.m_Device;
        m_Pipeline = other.m_Pipeline;
        m_PipelineLayout = other.m_PipelineLayout;
        m_DescriptorSetLayouts = std::move(other.m_DescriptorSetLayouts);
        m_IsGraphics = other.m_IsGraphics;

        other.m_Device = nullptr;
        other.m_Pipeline = VK_NULL_HANDLE;
        other.m_PipelineLayout = VK_NULL_HANDLE;
    }
    return *this;
}

// ===========================================================================================
// Graphics Pipeline Creation
// ===========================================================================================

bool VulkanPipeline::CreateGraphicsPipeline(const GraphicsPipelineConfig& config) {
    if (!config.device) {
        spdlog::error("[VulkanPipeline] Device is null");
        return false;
    }
    if (!config.shader) {
        spdlog::error("[VulkanPipeline] Shader is null");
        return false;
    }
    if (!config.renderPass) {
        spdlog::error("[VulkanPipeline] RenderPass is null");
        return false;
    }

    m_Device = config.device;
    m_IsGraphics = true;
    m_DescriptorSetLayouts = config.descriptorSetLayouts;

    try {
        // Create pipeline layout
        m_PipelineLayout = CreatePipelineLayout(config.descriptorSetLayouts,
                                               config.pushConstantRanges);
        if (!m_PipelineLayout) {
            spdlog::error("[VulkanPipeline] Failed to create pipeline layout");
            return false;
        }

        // Get shader stage info
        const auto stageInfos = config.shader->GetStageCreateInfos();
        if (stageInfos.empty()) {
            spdlog::error("[VulkanPipeline] No shader stages provided");
            return false;
        }

        // ===== Vertex Input State =====
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
        vertexInputInfo.vertexBindingDescriptionCount = 
            static_cast<uint32_t>(config.vertexBindings.size());
        vertexInputInfo.pVertexBindingDescriptions = 
            config.vertexBindings.empty() ? nullptr : config.vertexBindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = 
            static_cast<uint32_t>(config.vertexAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = 
            config.vertexAttributes.empty() ? nullptr : config.vertexAttributes.data();

        // ===== Input Assembly State =====
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
        inputAssemblyInfo.topology = config.inputAssembly.topology;
        inputAssemblyInfo.primitiveRestartEnable = config.inputAssembly.primitiveRestartEnable;

        // ===== Viewport State =====
        // Note: If not in dynamic states, a default viewport/scissor should be provided.
        // For now, we assume viewport and scissor are set dynamically or will be set later.
        vk::PipelineViewportStateCreateInfo viewportStateInfo;
        viewportStateInfo.viewportCount = 1;
        viewportStateInfo.scissorCount = 1;
        
        // Dummy viewport and scissor (will be overridden if in dynamic states)
        vk::Viewport viewport{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 1.0f};
        vk::Rect2D scissor{{0, 0}, {800, 600}};
        
        // Check if viewport/scissor are in dynamic states
        bool hasViewportDynamic = false;
        bool hasScissorDynamic = false;
        for (const auto& state : config.dynamicState.dynamicStates) {
            if (state == vk::DynamicState::eViewport) hasViewportDynamic = true;
            if (state == vk::DynamicState::eScissor) hasScissorDynamic = true;
        }

        if (!hasViewportDynamic) {
            viewportStateInfo.pViewports = &viewport;
        }
        if (!hasScissorDynamic) {
            viewportStateInfo.pScissors = &scissor;
        }

        // ===== Rasterization State =====
        vk::PipelineRasterizationStateCreateInfo rasterizerInfo;
        rasterizerInfo.depthClampEnable = config.rasterizer.depthClampEnable;
        rasterizerInfo.rasterizerDiscardEnable = config.rasterizer.rasterizerDiscardEnable;
        rasterizerInfo.polygonMode = config.rasterizer.polygonMode;
        rasterizerInfo.cullMode = config.rasterizer.cullMode;
        rasterizerInfo.frontFace = config.rasterizer.frontFace;
        rasterizerInfo.depthBiasEnable = config.rasterizer.depthBiasEnable;
        rasterizerInfo.depthBiasConstantFactor = config.rasterizer.depthBiasConstantFactor;
        rasterizerInfo.depthBiasClamp = config.rasterizer.depthBiasClamp;
        rasterizerInfo.depthBiasSlopeFactor = config.rasterizer.depthBiasSlopeFactor;
        rasterizerInfo.lineWidth = config.rasterizer.lineWidth;

        // ===== Multisampling State =====
        vk::PipelineMultisampleStateCreateInfo multisamplingInfo;
        multisamplingInfo.rasterizationSamples = config.multisampling.rasterizationSamples;
        multisamplingInfo.sampleShadingEnable = config.multisampling.sampleShadingEnable;
        multisamplingInfo.minSampleShading = config.multisampling.minSampleShading;
        multisamplingInfo.alphaToCoverageEnable = config.multisampling.alphaToCoverageEnable;
        multisamplingInfo.alphaToOneEnable = config.multisampling.alphaToOneEnable;

        // ===== Depth Stencil State =====
        vk::PipelineDepthStencilStateCreateInfo depthStencilInfo;
        depthStencilInfo.depthTestEnable = config.depthStencil.depthTestEnable;
        depthStencilInfo.depthWriteEnable = config.depthStencil.depthWriteEnable;
        depthStencilInfo.depthCompareOp = config.depthStencil.depthCompareOp;
        depthStencilInfo.depthBoundsTestEnable = config.depthStencil.depthBoundsTestEnable;
        depthStencilInfo.minDepthBounds = config.depthStencil.minDepthBounds;
        depthStencilInfo.maxDepthBounds = config.depthStencil.maxDepthBounds;
        depthStencilInfo.stencilTestEnable = config.depthStencil.stencilTestEnable;
        depthStencilInfo.front = config.depthStencil.front;
        depthStencilInfo.back = config.depthStencil.back;

        // ===== Color Blend State =====
        std::vector<vk::PipelineColorBlendAttachmentState> blendAttachments;
        for (const auto& attachment : config.colorBlend.attachments) {
            vk::PipelineColorBlendAttachmentState vkAttachment;
            vkAttachment.blendEnable = attachment.blendEnable;
            vkAttachment.srcColorBlendFactor = attachment.srcColorBlendFactor;
            vkAttachment.dstColorBlendFactor = attachment.dstColorBlendFactor;
            vkAttachment.colorBlendOp = attachment.colorBlendOp;
            vkAttachment.srcAlphaBlendFactor = attachment.srcAlphaBlendFactor;
            vkAttachment.dstAlphaBlendFactor = attachment.dstAlphaBlendFactor;
            vkAttachment.alphaBlendOp = attachment.alphaBlendOp;
            vkAttachment.colorWriteMask = attachment.colorWriteMask;
            blendAttachments.push_back(vkAttachment);
        }

        // If no attachments specified, create a default one
        if (blendAttachments.empty()) {
            ColorBlendAttachmentState defaultAttachment;
            vk::PipelineColorBlendAttachmentState vkAttachment;
            vkAttachment.blendEnable = defaultAttachment.blendEnable;
            vkAttachment.srcColorBlendFactor = defaultAttachment.srcColorBlendFactor;
            vkAttachment.dstColorBlendFactor = defaultAttachment.dstColorBlendFactor;
            vkAttachment.colorBlendOp = defaultAttachment.colorBlendOp;
            vkAttachment.srcAlphaBlendFactor = defaultAttachment.srcAlphaBlendFactor;
            vkAttachment.dstAlphaBlendFactor = defaultAttachment.dstAlphaBlendFactor;
            vkAttachment.alphaBlendOp = defaultAttachment.alphaBlendOp;
            vkAttachment.colorWriteMask = defaultAttachment.colorWriteMask;
            blendAttachments.push_back(vkAttachment);
        }

        vk::PipelineColorBlendStateCreateInfo colorBlendInfo;
        colorBlendInfo.logicOpEnable = config.colorBlend.logicOpEnable;
        colorBlendInfo.logicOp = config.colorBlend.logicOp;
        colorBlendInfo.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
        colorBlendInfo.pAttachments = blendAttachments.data();
        colorBlendInfo.blendConstants = config.colorBlend.blendConstants;

        // ===== Dynamic State =====
        vk::PipelineDynamicStateCreateInfo dynamicStateInfo;
        dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(config.dynamicState.dynamicStates.size());
        dynamicStateInfo.pDynamicStates = config.dynamicState.dynamicStates.empty() 
            ? nullptr 
            : config.dynamicState.dynamicStates.data();

        // ===== Create Graphics Pipeline =====
        vk::GraphicsPipelineCreateInfo pipelineInfo;
        pipelineInfo.stageCount = static_cast<uint32_t>(stageInfos.size());
        pipelineInfo.pStages = stageInfos.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
        pipelineInfo.pViewportState = &viewportStateInfo;
        pipelineInfo.pRasterizationState = &rasterizerInfo;
        pipelineInfo.pMultisampleState = &multisamplingInfo;
        pipelineInfo.pDepthStencilState = &depthStencilInfo;
        pipelineInfo.pColorBlendState = &colorBlendInfo;
        pipelineInfo.pDynamicState = dynamicStateInfo.dynamicStateCount > 0 ? &dynamicStateInfo : nullptr;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = config.renderPass->GetRenderPass();
        pipelineInfo.subpass = config.subpassIndex;

        m_Pipeline = m_Device->GetDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;

        if (!m_Pipeline) {
            spdlog::error("[VulkanPipeline] Failed to create graphics pipeline");
            return false;
        }

        spdlog::info("[VulkanPipeline] Successfully created graphics pipeline");
        return true;

    } catch (const vk::SystemError& e) {
        spdlog::error("[VulkanPipeline] Graphics pipeline creation failed: {}", e.what());
        Destroy();
        return false;
    }
}

// ===========================================================================================
// Compute Pipeline Creation
// ===========================================================================================

bool VulkanPipeline::CreateComputePipeline(const ComputePipelineConfig& config) {
    if (!config.device) {
        spdlog::error("[VulkanPipeline] Device is null");
        return false;
    }
    if (!config.shader) {
        spdlog::error("[VulkanPipeline] Shader is null");
        return false;
    }
    if (!config.shader->HasStage(ShaderStage::Compute)) {
        spdlog::error("[VulkanPipeline] Shader does not have compute stage");
        return false;
    }

    m_Device = config.device;
    m_IsGraphics = false;
    m_DescriptorSetLayouts = config.descriptorSetLayouts;

    try {
        // Create pipeline layout
        m_PipelineLayout = CreatePipelineLayout(config.descriptorSetLayouts,
                                               config.pushConstantRanges);
        if (!m_PipelineLayout) {
            spdlog::error("[VulkanPipeline] Failed to create pipeline layout");
            return false;
        }

        // Get compute shader stage
        vk::ShaderModule computeModule = config.shader->GetModule(ShaderStage::Compute);
        if (!computeModule) {
            spdlog::error("[VulkanPipeline] Compute shader module is invalid");
            return false;
        }

        vk::PipelineShaderStageCreateInfo stageInfo;
        stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
        stageInfo.module = computeModule;
        stageInfo.pName = "main";

        // ===== Create Compute Pipeline =====
        vk::ComputePipelineCreateInfo pipelineInfo;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = m_PipelineLayout;

        m_Pipeline = m_Device->GetDevice().createComputePipeline(nullptr, pipelineInfo).value;

        if (!m_Pipeline) {
            spdlog::error("[VulkanPipeline] Failed to create compute pipeline");
            return false;
        }

        spdlog::info("[VulkanPipeline] Successfully created compute pipeline");
        return true;

    } catch (const vk::SystemError& e) {
        spdlog::error("[VulkanPipeline] Compute pipeline creation failed: {}", e.what());
        Destroy();
        return false;
    }
}

// ===========================================================================================
// Binding and Execution
// ===========================================================================================

void VulkanPipeline::Bind(vk::CommandBuffer cmd) const {
    if (!m_Pipeline) {
        spdlog::warn("[VulkanPipeline] Attempting to bind null pipeline");
        return;
    }

    const vk::PipelineBindPoint bindPoint = m_IsGraphics 
        ? vk::PipelineBindPoint::eGraphics 
        : vk::PipelineBindPoint::eCompute;

    cmd.bindPipeline(bindPoint, m_Pipeline);
}

void VulkanPipeline::BindDescriptorSet(vk::CommandBuffer cmd,
                                       uint32_t setIndex,
                                       vk::DescriptorSet descriptorSet) const {
    if (!m_Pipeline || !m_PipelineLayout) {
        spdlog::warn("[VulkanPipeline] Attempting to bind descriptor set with invalid pipeline");
        return;
    }

    const vk::PipelineBindPoint bindPoint = m_IsGraphics 
        ? vk::PipelineBindPoint::eGraphics 
        : vk::PipelineBindPoint::eCompute;

    cmd.bindDescriptorSets(bindPoint, m_PipelineLayout, setIndex, 
                          {descriptorSet}, {});
}

void VulkanPipeline::BindDescriptorSets(vk::CommandBuffer cmd,
                                        uint32_t firstSetIndex,
                                        const std::vector<vk::DescriptorSet>& descriptorSets) const {
    if (!m_Pipeline || !m_PipelineLayout) {
        spdlog::warn("[VulkanPipeline] Attempting to bind descriptor sets with invalid pipeline");
        return;
    }

    if (descriptorSets.empty()) {
        return;
    }

    const vk::PipelineBindPoint bindPoint = m_IsGraphics 
        ? vk::PipelineBindPoint::eGraphics 
        : vk::PipelineBindPoint::eCompute;

    cmd.bindDescriptorSets(bindPoint, m_PipelineLayout, firstSetIndex,
                          descriptorSets, {});
}

void VulkanPipeline::PushConstants(vk::CommandBuffer cmd,
                                   vk::ShaderStageFlags stageFlags,
                                   uint32_t offset,
                                   uint32_t size,
                                   const void* data) const {
    if (!m_PipelineLayout) {
        spdlog::warn("[VulkanPipeline] Attempting to push constants with invalid pipeline layout");
        return;
    }

    cmd.pushConstants(m_PipelineLayout, stageFlags, offset, size, data);
}

// ===========================================================================================
// Cleanup
// ===========================================================================================

void VulkanPipeline::Destroy() {
    if (!m_Device) {
        return;
    }

    const vk::Device device = m_Device->GetDevice();

    if (m_Pipeline) {
        device.destroyPipeline(m_Pipeline);
        m_Pipeline = VK_NULL_HANDLE;
    }

    if (m_PipelineLayout) {
        device.destroyPipelineLayout(m_PipelineLayout);
        m_PipelineLayout = VK_NULL_HANDLE;
    }

    // Note: DescriptorSetLayouts are typically owned by the descriptor set manager
    // and should not be destroyed here. Clear the vector but don't destroy the layouts.
    m_DescriptorSetLayouts.clear();

    m_Device = nullptr;
}

// ===========================================================================================
// Helper Methods
// ===========================================================================================

vk::PipelineLayout VulkanPipeline::CreatePipelineLayout(
    const std::vector<vk::DescriptorSetLayout>& descriptorSetLayouts,
    const std::vector<vk::PushConstantRange>& pushConstantRanges)
{
    try {
        vk::PipelineLayoutCreateInfo layoutInfo;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
        layoutInfo.pSetLayouts = descriptorSetLayouts.empty() ? nullptr : descriptorSetLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
        layoutInfo.pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data();

        vk::PipelineLayout layout = m_Device->GetDevice().createPipelineLayout(layoutInfo);

        if (!layout) {
            spdlog::error("[VulkanPipeline] Failed to create pipeline layout");
            return VK_NULL_HANDLE;
        }

        return layout;

    } catch (const vk::SystemError& e) {
        spdlog::error("[VulkanPipeline] Pipeline layout creation failed: {}", e.what());
        return VK_NULL_HANDLE;
    }
}
