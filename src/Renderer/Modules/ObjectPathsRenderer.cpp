#include "ObjectPathsRenderer.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Simulation/Scene.h"
#include "Renderer/Interface/Camera.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/type_ptr.hpp>

void ObjectPathsRenderer::Init(VulkanDevice* device, VulkanRenderPass* renderPass) {
    if (!device) {
        spdlog::error("ObjectPathsRenderer::Init: Device is null");
        return;
    }
    
    m_Device = device;
    
    // Load shaders
    try {
        m_shader = std::make_unique<VulkanShader>(device, std::vector<std::pair<ShaderStage, std::string>>{
            {ShaderStage::Vertex, "shaders/object_paths.vert"},
            {ShaderStage::Fragment, "shaders/object_paths.frag"}
        });
    } catch (const std::exception& e) {
        spdlog::error("ObjectPathsRenderer::Init: Failed to load shaders: {}", e.what());
        return;
    }
    
    // Create graphics pipeline
    GraphicsPipelineConfig pipelineConfig;
    pipelineConfig.device = device;
    pipelineConfig.shader = m_shader.get();
    pipelineConfig.renderPass = renderPass;
    
    // Setup vertex input: Position only (vec3)
    vk::VertexInputBindingDescription bindingDesc;
    bindingDesc.binding = 0;
    bindingDesc.stride = 3 * sizeof(float);  // Position (xyz)
    bindingDesc.inputRate = vk::VertexInputRate::eVertex;
    pipelineConfig.vertexBindings.push_back(bindingDesc);
    
    vk::VertexInputAttributeDescription attrDesc;
    attrDesc.binding = 0;
    attrDesc.location = 0;
    attrDesc.format = vk::Format::eR32G32B32Sfloat;  // vec3
    attrDesc.offset = 0;
    pipelineConfig.vertexAttributes.push_back(attrDesc);
    
    // Input assembly: Line strip topology for continuous paths
    pipelineConfig.inputAssembly.topology = vk::PrimitiveTopology::eLineStrip;
    pipelineConfig.inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // Rasterizer state
    pipelineConfig.rasterizer.depthClampEnable = VK_FALSE;
    pipelineConfig.rasterizer.rasterizerDiscardEnable = VK_FALSE;
    pipelineConfig.rasterizer.polygonMode = vk::PolygonMode::eFill;
    pipelineConfig.rasterizer.cullMode = vk::CullModeFlagBits::eNone;  // No culling for lines
    pipelineConfig.rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    pipelineConfig.rasterizer.depthBiasEnable = VK_FALSE;
    pipelineConfig.rasterizer.lineWidth = m_lineThickness;
    
    // Depth stencil: Disable depth test for overlay paths
    pipelineConfig.depthStencil.depthTestEnable = VK_FALSE;
    pipelineConfig.depthStencil.depthWriteEnable = VK_FALSE;
    
    // Color blend: Enable alpha blending
    ColorBlendAttachmentState blendAttachment;
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    blendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    blendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    blendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    blendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    blendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
    
    pipelineConfig.colorBlend.attachments.push_back(blendAttachment);
    
    // Push constants for MVP matrix, color, and opacity
    vk::PushConstantRange pushConstantRange;
    pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4) + sizeof(glm::vec3) + sizeof(float);  // MVP + color + opacity
    pipelineConfig.pushConstantRanges.push_back(pushConstantRange);
    
    m_pipeline = std::make_unique<VulkanPipeline>();
    if (!m_pipeline->CreateGraphicsPipeline(pipelineConfig)) {
        spdlog::error("ObjectPathsRenderer::Init: Failed to create graphics pipeline");
        return;
    }
    
    // Create VBO with initial size
    VulkanBufferSpec vboSpec;
    vboSpec.Size = 10000 * 3 * sizeof(float);  // Initial capacity for 10k vertices
    vboSpec.Usage = vk::BufferUsageFlagBits::eVertexBuffer;
    vboSpec.MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    
    m_vbo = std::make_unique<VulkanBuffer>();
    m_vbo->Create(vboSpec, device);
    
    spdlog::info("ObjectPathsRenderer initialized");
}

void ObjectPathsRenderer::RecordCurrentPositions(Scene* scene) {
    if (!scene) return;
    
    if (scene != m_cachedScene) {
        ClearHistories();
        m_cachedScene = scene;
    }

    size_t meshCount = 0;
    size_t sphereCount = 0;
    for (const auto& obj : scene->objects) {
        if (obj.HasClass("Mesh")) meshCount++;
        if (obj.HasClass("Sphere")) sphereCount++;
    }

    if (m_meshHistories.size() != meshCount) {
        m_meshHistories.resize(meshCount);
    }
    
    if (m_sphereHistories.size() != sphereCount) {
        m_sphereHistories.resize(sphereCount);
    }

    size_t meshIdx = 0;
    for (const auto& obj : scene->objects) {
        if (obj.HasClass("Mesh") && meshIdx < m_meshHistories.size()) {
            ParameterHandle posHandle("Entity.Position");
            auto pos = obj.GetParameter(posHandle);

            if (std::holds_alternative<glm::vec3>(pos)) {
                auto& history = m_meshHistories[meshIdx];
                history.positions.push_back(std::get<glm::vec3>(pos));

                if (history.positions.size() > m_maxHistorySize) {
                    history.positions.pop_front();
                }
            }
            meshIdx++;
        }
    }

    size_t sphereIdx = 0;
    for (const auto& obj : scene->objects) {
        if (obj.HasClass("Sphere") && sphereIdx < m_sphereHistories.size()) {
            ParameterHandle posHandle("Entity.Position");
            auto pos = obj.GetParameter(posHandle);

            if (std::holds_alternative<glm::vec3>(pos)) {
                auto& history = m_sphereHistories[sphereIdx];
                history.positions.push_back(std::get<glm::vec3>(pos));

                if (history.positions.size() > m_maxHistorySize) {
                    history.positions.pop_front();
                }
            }
            sphereIdx++;
        }
    }
}

void ObjectPathsRenderer::ClearHistories() {
    m_meshHistories.clear();
    m_sphereHistories.clear();
    m_cachedScene = nullptr;
}

void ObjectPathsRenderer::UpdateBuffers() {
    m_lineVertices.clear();
    
    // Collect all vertex data: mesh paths first, then sphere paths
    for (const auto& history : m_meshHistories) {
        for (const auto& pos : history.positions) {
            m_lineVertices.push_back(pos.x);
            m_lineVertices.push_back(pos.y);
            m_lineVertices.push_back(pos.z);
        }
    }
    
    for (const auto& history : m_sphereHistories) {
        for (const auto& pos : history.positions) {
            m_lineVertices.push_back(pos.x);
            m_lineVertices.push_back(pos.y);
            m_lineVertices.push_back(pos.z);
        }
    }
    
    m_vertexCount = static_cast<int>(m_lineVertices.size() / 3);
    
    // Update VBO if we have vertices
    if (m_vertexCount > 0) {
        size_t requiredSize = m_lineVertices.size() * sizeof(float);
        
        // Resize buffer if needed
        if (requiredSize > m_vbo->GetSize()) {
            m_vbo->Destroy();
            
            VulkanBufferSpec spec;
            spec.Size = requiredSize * 2;  // Double the required size for future growth
            spec.Usage = vk::BufferUsageFlagBits::eVertexBuffer;
            spec.MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            
            m_vbo->Create(spec, m_Device);
        }
        
        // Write vertex data to VBO
        m_vbo->Write(m_lineVertices.data(), m_lineVertices.size() * sizeof(float));
    }
}

void ObjectPathsRenderer::Render(const Scene& /*scene*/, const Camera& camera, float /*time*/, vk::CommandBuffer cmd) {
    if (!m_Device || !m_shader || !m_pipeline || !m_vbo) {
        return;
    }
    
    // Update vertex buffer
    UpdateBuffers();
    
    if (m_vertexCount == 0) {
        return;
    }
    
    // Bind pipeline
    m_pipeline->Bind(cmd);
    
    // Bind vertex buffer
    vk::Buffer vertexBuffers[] = {m_vbo->GetBuffer()};
    vk::DeviceSize offsets[] = {0};
    cmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    
    // Prepare push constants
    struct PushConstantData {
        glm::mat4 mvp;
        glm::vec3 color;
        float opacity;
    } pushConstData;
    
    pushConstData.mvp = camera.GetViewProjectionMatrix();
    pushConstData.opacity = m_opacity;
    
    // Draw mesh paths
    int offset = 0;
    pushConstData.color = m_meshColor;
    
    for (const auto& history : m_meshHistories) {
        if (history.positions.size() > 1) {
            // Push constants for this draw call
            m_pipeline->PushConstants(
                cmd,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(PushConstantData),
                &pushConstData
            );
            
            // Draw the path
            cmd.draw(static_cast<uint32_t>(history.positions.size()), 1, offset, 0);
        }
        offset += static_cast<int>(history.positions.size());
    }
    
    // Draw sphere paths
    pushConstData.color = m_sphereColor;
    
    for (const auto& history : m_sphereHistories) {
        if (history.positions.size() > 1) {
            // Push constants for this draw call
            m_pipeline->PushConstants(
                cmd,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(PushConstantData),
                &pushConstData
            );
            
            // Draw the path
            cmd.draw(static_cast<uint32_t>(history.positions.size()), 1, offset, 0);
        }
        offset += static_cast<int>(history.positions.size());
    }
}
