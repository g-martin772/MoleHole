#include "PhysicsDebugRenderer.h"
#include "Renderer/Interface/Camera.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <spdlog/spdlog.h>

// Vertex layout for debug lines and triangles: vec3 position + vec3 color
struct DebugVertex {
    glm::vec3 position;
    glm::vec3 color;
};

// Push constant structure: ViewProjection matrix (64 bytes)
struct MVPPushConstant {
    glm::mat4 viewProjection;
};

void PhysicsDebugRenderer::Init(VulkanDevice* device, VulkanRenderPass* renderPass) {
    if (!device) {
        spdlog::error("PhysicsDebugRenderer::Init: device is nullptr");
        return;
    }

    m_Device = device;

    // Create VBOs for lines and triangles (using HostVisible for dynamic updates)
    VulkanBufferSpec lineVBOSpec{
        .Size = MAX_LINES * 2 * sizeof(DebugVertex),  // 2 vertices per line
        .Usage = vk::BufferUsageFlagBits::eVertexBuffer,
        .MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    };
    m_LineVBO = std::make_unique<VulkanBuffer>();
    m_LineVBO->Create(lineVBOSpec, device);

    VulkanBufferSpec triangleVBOSpec{
        .Size = MAX_TRIANGLES * 3 * sizeof(DebugVertex),  // 3 vertices per triangle
        .Usage = vk::BufferUsageFlagBits::eVertexBuffer,
        .MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    };
    m_TriangleVBO = std::make_unique<VulkanBuffer>();
    m_TriangleVBO->Create(triangleVBOSpec, device);

    // Load shaders for lines
    try {
        m_LineShader = std::make_unique<VulkanShader>(device, std::initializer_list<std::pair<ShaderStage, std::string>>{
            {ShaderStage::Vertex, "shaders/physics_debug_line.vert"},
            {ShaderStage::Fragment, "shaders/physics_debug_line.frag"}
        });
    } catch (const std::exception& e) {
        spdlog::error("Failed to load line shader: {}", e.what());
        return;
    }

    // Load shaders for triangles
    try {
        m_TriangleShader = std::make_unique<VulkanShader>(device, std::initializer_list<std::pair<ShaderStage, std::string>>{
            {ShaderStage::Vertex, "shaders/physics_debug_triangle.vert"},
            {ShaderStage::Fragment, "shaders/physics_debug_triangle.frag"}
        });
    } catch (const std::exception& e) {
        spdlog::error("Failed to load triangle shader: {}", e.what());
        return;
    }

    // Vertex input descriptions for DebugVertex
    std::vector<vk::VertexInputBindingDescription> vertexBindings;
    {
        vk::VertexInputBindingDescription binding;
        binding.binding = 0;
        binding.stride = sizeof(DebugVertex);
        binding.inputRate = vk::VertexInputRate::eVertex;
        vertexBindings.push_back(binding);
    }

    std::vector<vk::VertexInputAttributeDescription> vertexAttributes;
    {
        vk::VertexInputAttributeDescription posAttr;
        posAttr.location = 0;
        posAttr.binding = 0;
        posAttr.format = vk::Format::eR32G32B32Sfloat;
        posAttr.offset = offsetof(DebugVertex, position);
        vertexAttributes.push_back(posAttr);

        vk::VertexInputAttributeDescription colorAttr;
        colorAttr.location = 1;
        colorAttr.binding = 0;
        colorAttr.format = vk::Format::eR32G32B32Sfloat;
        colorAttr.offset = offsetof(DebugVertex, color);
        vertexAttributes.push_back(colorAttr);
    }

    // Push constant range for MVP matrix
    std::vector<vk::PushConstantRange> pushConstantRanges;
    {
        vk::PushConstantRange pcRange;
        pcRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
        pcRange.offset = 0;
        pcRange.size = sizeof(MVPPushConstant);
        pushConstantRanges.push_back(pcRange);
    }

    // Create line pipeline
    {
        GraphicsPipelineConfig linePipelineConfig;
        linePipelineConfig.device = device;
        linePipelineConfig.shader = m_LineShader.get();
        linePipelineConfig.renderPass = renderPass;
        linePipelineConfig.subpassIndex = 0;
        linePipelineConfig.vertexBindings = vertexBindings;
        linePipelineConfig.vertexAttributes = vertexAttributes;
        linePipelineConfig.pushConstantRanges = pushConstantRanges;

        linePipelineConfig.inputAssembly.topology = vk::PrimitiveTopology::eLineList;
        linePipelineConfig.inputAssembly.primitiveRestartEnable = VK_FALSE;

        linePipelineConfig.rasterizer.polygonMode = vk::PolygonMode::eLine;
        linePipelineConfig.rasterizer.lineWidth = 1.0f;
        linePipelineConfig.rasterizer.cullMode = vk::CullModeFlagBits::eNone;

        linePipelineConfig.depthStencil.depthTestEnable = VK_TRUE;
        linePipelineConfig.depthStencil.depthWriteEnable = VK_FALSE;
        linePipelineConfig.depthStencil.depthCompareOp = vk::CompareOp::eLess;

        linePipelineConfig.colorBlend.attachments.push_back(ColorBlendAttachmentState{});

        m_LinePipeline = std::make_unique<VulkanPipeline>();
        if (!m_LinePipeline->CreateGraphicsPipeline(linePipelineConfig)) {
            spdlog::error("PhysicsDebugRenderer::Init: Failed to create line pipeline");
        }
    }

    // Create triangle pipeline
    {
        GraphicsPipelineConfig trianglePipelineConfig;
        trianglePipelineConfig.device = device;
        trianglePipelineConfig.shader = m_TriangleShader.get();
        trianglePipelineConfig.renderPass = renderPass;
        trianglePipelineConfig.subpassIndex = 0;
        trianglePipelineConfig.vertexBindings = vertexBindings;
        trianglePipelineConfig.vertexAttributes = vertexAttributes;
        trianglePipelineConfig.pushConstantRanges = pushConstantRanges;

        trianglePipelineConfig.inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        trianglePipelineConfig.inputAssembly.primitiveRestartEnable = VK_FALSE;

        trianglePipelineConfig.rasterizer.polygonMode = vk::PolygonMode::eLine;  // Wireframe
        trianglePipelineConfig.rasterizer.lineWidth = 1.0f;
        trianglePipelineConfig.rasterizer.cullMode = vk::CullModeFlagBits::eNone;

        trianglePipelineConfig.depthStencil.depthTestEnable = VK_TRUE;
        trianglePipelineConfig.depthStencil.depthWriteEnable = VK_FALSE;
        trianglePipelineConfig.depthStencil.depthCompareOp = vk::CompareOp::eLess;

        trianglePipelineConfig.colorBlend.attachments.push_back(ColorBlendAttachmentState{});

        m_TrianglePipeline = std::make_unique<VulkanPipeline>();
        if (!m_TrianglePipeline->CreateGraphicsPipeline(trianglePipelineConfig)) {
             spdlog::error("PhysicsDebugRenderer::Init: Failed to create triangle pipeline");
        }
    }

    spdlog::info("PhysicsDebugRenderer initialized");
}

void PhysicsDebugRenderer::Shutdown() {
    if (m_LineVBO) {
        m_LineVBO->Destroy();
        m_LineVBO.reset();
    }

    if (m_TriangleVBO) {
        m_TriangleVBO->Destroy();
        m_TriangleVBO.reset();
    }

    if (m_LinePipeline) {
        m_LinePipeline->Destroy();
        m_LinePipeline.reset();
    }

    if (m_TrianglePipeline) {
        m_TrianglePipeline->Destroy();
        m_TrianglePipeline.reset();
    }

    m_LineShader.reset();
    m_TriangleShader.reset();
    m_Device = nullptr;

    spdlog::info("PhysicsDebugRenderer shutdown");
}

void PhysicsDebugRenderer::Render(const PxRenderBuffer* renderBuffer, Camera* camera, vk::CommandBuffer cmd) {
    if (!m_Enabled || !camera || !renderBuffer || !m_Device) {
        return;
    }

    static int frameCounter = 0;
    if (frameCounter % 60 == 0) {
        spdlog::debug("PhysX Debug: {} lines, {} triangles, {} points",
                     renderBuffer->getNbLines(),
                     renderBuffer->getNbTriangles(),
                     renderBuffer->getNbPoints());
    }
    frameCounter++;

    if (renderBuffer->getNbLines() > 0) {
        RenderLines(renderBuffer->getLines(), renderBuffer->getNbLines(), camera, cmd);
    }

    if (renderBuffer->getNbTriangles() > 0) {
        RenderTriangles(renderBuffer->getTriangles(), renderBuffer->getNbTriangles(), camera, cmd);
    }
}

void PhysicsDebugRenderer::RenderLines(const PxDebugLine* lines, uint32_t count, Camera* camera, vk::CommandBuffer cmd) {
    if (count == 0 || !m_LineVBO || !m_LineShader || !m_LinePipeline || !m_Device) {
        return;
    }

    count = std::min(count, static_cast<uint32_t>(MAX_LINES));

    // Convert PhysX debug lines to vertex data
    std::vector<DebugVertex> vertexData;
    vertexData.reserve(count * 2);

    for (uint32_t i = 0; i < count; ++i) {
        const PxDebugLine& line = lines[i];

        // Convert color0 from uint32_t (ARGB or similar) to float RGB
        uint32_t color0 = line.color0;
        float r0 = ((color0 >> 16) & 0xFF) / 255.0f;
        float g0 = ((color0 >> 8) & 0xFF) / 255.0f;
        float b0 = (color0 & 0xFF) / 255.0f;

        // Convert color1
        uint32_t color1 = line.color1;
        float r1 = ((color1 >> 16) & 0xFF) / 255.0f;
        float g1 = ((color1 >> 8) & 0xFF) / 255.0f;
        float b1 = (color1 & 0xFF) / 255.0f;

        vertexData.push_back(DebugVertex{glm::vec3(line.pos0.x, line.pos0.y, line.pos0.z), glm::vec3(r0, g0, b0)});
        vertexData.push_back(DebugVertex{glm::vec3(line.pos1.x, line.pos1.y, line.pos1.z), glm::vec3(r1, g1, b1)});
    }

    // Update VBO with vertex data
    if (!vertexData.empty()) {
        m_LineVBO->Write(vertexData.data(), vertexData.size() * sizeof(DebugVertex), 0);
    }

    // Bind pipeline
    m_LinePipeline->Bind(cmd);

    // Bind vertex buffer
    vk::Buffer vertexBuffer = m_LineVBO->GetBuffer();
    vk::DeviceSize offset = 0;
    cmd.bindVertexBuffers(0, 1, &vertexBuffer, &offset);

    // Push constants
    MVPPushConstant pushConstant;
    pushConstant.viewProjection = camera->GetViewProjectionMatrix();
    m_LinePipeline->PushConstants(
        cmd,
        vk::ShaderStageFlagBits::eVertex,
        0,
        sizeof(MVPPushConstant),
        &pushConstant
    );

    // Draw
    cmd.draw(static_cast<uint32_t>(vertexData.size()), 1, 0, 0);
}

void PhysicsDebugRenderer::RenderTriangles(const PxDebugTriangle* triangles, uint32_t count, Camera* camera, vk::CommandBuffer cmd) {
    if (count == 0 || !m_TriangleVBO || !m_TriangleShader || !m_TrianglePipeline || !m_Device) {
        return;
    }

    count = std::min(count, static_cast<uint32_t>(MAX_TRIANGLES));

    // Convert PhysX debug triangles to vertex data
    std::vector<DebugVertex> vertexData;
    vertexData.reserve(count * 3);

    for (uint32_t i = 0; i < count; ++i) {
        const PxDebugTriangle& tri = triangles[i];

        // Convert color0
        uint32_t color0 = tri.color0;
        float r0 = ((color0 >> 16) & 0xFF) / 255.0f;
        float g0 = ((color0 >> 8) & 0xFF) / 255.0f;
        float b0 = (color0 & 0xFF) / 255.0f;

        // Convert color1
        uint32_t color1 = tri.color1;
        float r1 = ((color1 >> 16) & 0xFF) / 255.0f;
        float g1 = ((color1 >> 8) & 0xFF) / 255.0f;
        float b1 = (color1 & 0xFF) / 255.0f;

        // Convert color2
        uint32_t color2 = tri.color2;
        float r2 = ((color2 >> 16) & 0xFF) / 255.0f;
        float g2 = ((color2 >> 8) & 0xFF) / 255.0f;
        float b2 = (color2 & 0xFF) / 255.0f;

        vertexData.push_back(DebugVertex{glm::vec3(tri.pos0.x, tri.pos0.y, tri.pos0.z), glm::vec3(r0, g0, b0)});
        vertexData.push_back(DebugVertex{glm::vec3(tri.pos1.x, tri.pos1.y, tri.pos1.z), glm::vec3(r1, g1, b1)});
        vertexData.push_back(DebugVertex{glm::vec3(tri.pos2.x, tri.pos2.y, tri.pos2.z), glm::vec3(r2, g2, b2)});
    }

    // Update VBO with vertex data
    if (!vertexData.empty()) {
        m_TriangleVBO->Write(vertexData.data(), vertexData.size() * sizeof(DebugVertex), 0);
    }

    // Bind pipeline
    m_TrianglePipeline->Bind(cmd);

    // Bind vertex buffer
    vk::Buffer vertexBuffer = m_TriangleVBO->GetBuffer();
    vk::DeviceSize offset = 0;
    cmd.bindVertexBuffers(0, 1, &vertexBuffer, &offset);

    // Push constants
    MVPPushConstant pushConstant;
    pushConstant.viewProjection = camera->GetViewProjectionMatrix();
    m_TrianglePipeline->PushConstants(
        cmd,
        vk::ShaderStageFlagBits::eVertex,
        0,
        sizeof(MVPPushConstant),
        &pushConstant
    );

    // Draw
    cmd.draw(static_cast<uint32_t>(vertexData.size()), 1, 0, 0);
}
