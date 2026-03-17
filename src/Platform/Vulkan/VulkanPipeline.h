#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <memory>

class VulkanDevice;
class VulkanShader;
class VulkanRenderPass;

// ===========================================================================================
// Pipeline State Structures
// ===========================================================================================

struct InputAssemblyState {
    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    vk::Bool32 primitiveRestartEnable = VK_FALSE;
};

struct RasterizerState {
    vk::Bool32 depthClampEnable = VK_FALSE;
    vk::Bool32 rasterizerDiscardEnable = VK_FALSE;
    vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
    vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
    vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
    vk::Bool32 depthBiasEnable = VK_FALSE;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
    float lineWidth = 1.0f;
};

struct MultisamplingState {
    vk::SampleCountFlagBits rasterizationSamples = vk::SampleCountFlagBits::e1;
    vk::Bool32 sampleShadingEnable = VK_FALSE;
    float minSampleShading = 1.0f;
    vk::Bool32 alphaToCoverageEnable = VK_FALSE;
    vk::Bool32 alphaToOneEnable = VK_FALSE;
};

struct DepthStencilState {
    vk::Bool32 depthTestEnable = VK_TRUE;
    vk::Bool32 depthWriteEnable = VK_TRUE;
    vk::CompareOp depthCompareOp = vk::CompareOp::eLess;
    vk::Bool32 depthBoundsTestEnable = VK_FALSE;
    float minDepthBounds = 0.0f;
    float maxDepthBounds = 1.0f;
    vk::Bool32 stencilTestEnable = VK_FALSE;
    vk::StencilOpState front = {};
    vk::StencilOpState back = {};
};

struct ColorBlendAttachmentState {
    vk::Bool32 blendEnable = VK_FALSE;
    vk::BlendFactor srcColorBlendFactor = vk::BlendFactor::eOne;
    vk::BlendFactor dstColorBlendFactor = vk::BlendFactor::eZero;
    vk::BlendOp colorBlendOp = vk::BlendOp::eAdd;
    vk::BlendFactor srcAlphaBlendFactor = vk::BlendFactor::eOne;
    vk::BlendFactor dstAlphaBlendFactor = vk::BlendFactor::eZero;
    vk::BlendOp alphaBlendOp = vk::BlendOp::eAdd;
    vk::ColorComponentFlags colorWriteMask = vk::ColorComponentFlagBits::eR |
                                             vk::ColorComponentFlagBits::eG |
                                             vk::ColorComponentFlagBits::eB |
                                             vk::ColorComponentFlagBits::eA;
};

struct ColorBlendState {
    vk::Bool32 logicOpEnable = VK_FALSE;
    vk::LogicOp logicOp = vk::LogicOp::eCopy;
    std::vector<ColorBlendAttachmentState> attachments;
    std::array<float, 4> blendConstants = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct DynamicStateConfig {
    std::vector<vk::DynamicState> dynamicStates;
};

// ===========================================================================================
// Pipeline Configuration Structures
// ===========================================================================================

struct GraphicsPipelineConfig {
    VulkanDevice* device = nullptr;
    VulkanShader* shader = nullptr;
    VulkanRenderPass* renderPass = nullptr;
    uint32_t subpassIndex = 0;

    // Vertex input (can be configured later or set to nullptr for simple cases)
    std::vector<vk::VertexInputBindingDescription> vertexBindings;
    std::vector<vk::VertexInputAttributeDescription> vertexAttributes;

    // Pipeline states
    InputAssemblyState inputAssembly;
    RasterizerState rasterizer;
    MultisamplingState multisampling;
    DepthStencilState depthStencil;
    ColorBlendState colorBlend;
    DynamicStateConfig dynamicState;

    // Descriptor set layouts (optional, or can be created from reflection)
    std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;

    // Push constants (optional)
    std::vector<vk::PushConstantRange> pushConstantRanges;
};

struct ComputePipelineConfig {
    VulkanDevice* device = nullptr;
    VulkanShader* shader = nullptr;  // Must have Compute stage

    // Descriptor set layouts (optional, or can be created from reflection)
    std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;

    // Push constants (optional)
    std::vector<vk::PushConstantRange> pushConstantRanges;
};

// ===========================================================================================
// VulkanPipeline Class
// ===========================================================================================

class VulkanPipeline {
public:
    // Constructors
    VulkanPipeline() = default;
    ~VulkanPipeline();

    // Delete copy operations
    VulkanPipeline(const VulkanPipeline&) = delete;
    VulkanPipeline& operator=(const VulkanPipeline&) = delete;

    // Move operations
    VulkanPipeline(VulkanPipeline&& other) noexcept;
    VulkanPipeline& operator=(VulkanPipeline&& other) noexcept;

    // ===========================================================================================
    // Graphics Pipeline Creation
    // ===========================================================================================

    /**
     * Create a graphics pipeline.
     * @param config Graphics pipeline configuration
     * @return true if creation was successful
     */
    bool CreateGraphicsPipeline(const GraphicsPipelineConfig& config);

    // ===========================================================================================
    // Compute Pipeline Creation
    // ===========================================================================================

    /**
     * Create a compute pipeline.
     * @param config Compute pipeline configuration
     * @return true if creation was successful
     */
    bool CreateComputePipeline(const ComputePipelineConfig& config);

    // ===========================================================================================
    // Getters
    // ===========================================================================================

    vk::Pipeline GetPipeline() const { return m_Pipeline; }
    vk::PipelineLayout GetLayout() const { return m_PipelineLayout; }
    const std::vector<vk::DescriptorSetLayout>& GetDescriptorSetLayouts() const {
        return m_DescriptorSetLayouts;
    }
    bool IsGraphicsPipeline() const { return m_IsGraphics; }
    bool IsComputePipeline() const { return !m_IsGraphics; }

    // ===========================================================================================
    // Binding and Execution
    // ===========================================================================================

    /**
     * Bind this pipeline to a command buffer.
     * @param cmd Command buffer to bind to
     */
    void Bind(vk::CommandBuffer cmd) const;

    /**
     * Bind a descriptor set to this pipeline.
     * @param cmd Command buffer
     * @param setIndex Descriptor set binding index
     * @param descriptorSet Descriptor set to bind
     */
    void BindDescriptorSet(vk::CommandBuffer cmd,
                           uint32_t setIndex,
                           vk::DescriptorSet descriptorSet) const;

    /**
     * Bind multiple descriptor sets to this pipeline.
     * @param cmd Command buffer
     * @param firstSetIndex Starting descriptor set binding index
     * @param descriptorSets Descriptor sets to bind
     */
    void BindDescriptorSets(vk::CommandBuffer cmd,
                            uint32_t firstSetIndex,
                            const std::vector<vk::DescriptorSet>& descriptorSets) const;

    /**
     * Push constants to this pipeline.
     * @param cmd Command buffer
     * @param stageFlags Which shader stages to push to
     * @param offset Offset into the push constant block
     * @param size Size of the data to push
     * @param data Pointer to the data to push
     */
    void PushConstants(vk::CommandBuffer cmd,
                       vk::ShaderStageFlags stageFlags,
                       uint32_t offset,
                       uint32_t size,
                       const void* data) const;

    // ===========================================================================================
    // Cleanup
    // ===========================================================================================

    /**
     * Destroy the pipeline and all associated resources.
     */
    void Destroy();

private:
    // ===========================================================================================
    // Helper Methods
    // ===========================================================================================

    /**
     * Create a pipeline layout from descriptor set layouts and push constant ranges.
     * @param descriptorSetLayouts Vector of descriptor set layouts
     * @param pushConstantRanges Vector of push constant ranges
     * @return Created pipeline layout, or VK_NULL_HANDLE on failure
     */
    vk::PipelineLayout CreatePipelineLayout(
        const std::vector<vk::DescriptorSetLayout>& descriptorSetLayouts,
        const std::vector<vk::PushConstantRange>& pushConstantRanges);

    // ===========================================================================================
    // Member Variables
    // ===========================================================================================

    VulkanDevice* m_Device = nullptr;
    vk::Pipeline m_Pipeline = VK_NULL_HANDLE;
    vk::PipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
    std::vector<vk::DescriptorSetLayout> m_DescriptorSetLayouts;
    bool m_IsGraphics = false;
};
