#include "GravityGridRenderer.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Simulation/Scene.h"
#include "Renderer/Interface/Camera.h"
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Must match shader layout (std140)
struct GravityGridUBO {
    glm::mat4 vp;
    glm::vec4 color; // w = opacity
    glm::vec4 params; // x = planeY, y = cellSize, z = lineThickness, w = unused
    
    int numBlackHoles;
    float _pad0[3];
    glm::vec4 blackHolePositions[8];
    glm::vec4 blackHoleMasses[8]; // w = mass (padded to vec4)
    
    int numSpheres;
    float _pad1[3];
    glm::vec4 spherePositions[16];
    glm::vec4 sphereMasses[16]; // w = mass
    
    int numMeshes;
    float _pad2[3];
    glm::vec4 meshPositions[8];
    glm::vec4 meshMasses[8]; // w = mass
};

void GravityGridRenderer::Init(VulkanDevice* device, VulkanRenderPass* renderPass) {
    m_Device = device;
    
    // Load shaders
    m_shader = std::make_unique<VulkanShader>(
        device,
        std::vector<std::pair<ShaderStage, std::string>>{
            {ShaderStage::Vertex, "shaders/plane_grid.vert"},
            {ShaderStage::Fragment, "shaders/plane_grid.frag"}
        }
    );
    
    // Create descriptor set layout
    vk::DescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    
    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;
    
    m_DescriptorSetLayout = device->GetDevice().createDescriptorSetLayout(layoutInfo);
    
    // Create descriptor pool
    vk::DescriptorPoolSize poolSize{vk::DescriptorType::eUniformBuffer, 1};
    vk::DescriptorPoolCreateInfo poolInfo{{}, 1, 1, &poolSize};
    m_DescriptorPool = device->GetDevice().createDescriptorPool(poolInfo);
    
    // Allocate descriptor set
    vk::DescriptorSetAllocateInfo allocInfo{m_DescriptorPool, 1, &m_DescriptorSetLayout};
    m_DescriptorSet = device->GetDevice().allocateDescriptorSets(allocInfo)[0];
    
    // Create UBO
    m_UBO = std::make_unique<VulkanBuffer>();
    VulkanBufferSpec uboSpec{
        sizeof(GravityGridUBO),
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    };
    m_UBO->Create(uboSpec, device);
    
    // Update descriptor set
    vk::DescriptorBufferInfo bufferInfo = m_UBO->GetDescriptorInfo();
    vk::WriteDescriptorSet descriptorWrite{
        m_DescriptorSet, 0, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufferInfo, nullptr
    };
    device->GetDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
    
    // Create Pipeline
    GraphicsPipelineConfig config;
    config.device = device;
    config.shader = m_shader.get();
    config.renderPass = renderPass;
    config.descriptorSetLayouts = { m_DescriptorSetLayout };
    
    config.vertexBindings = { {0, 3 * sizeof(float), vk::VertexInputRate::eVertex} };
    config.vertexAttributes = { {0, 0, vk::Format::eR32G32B32Sfloat, 0} };
    
    config.inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    config.rasterizer.polygonMode = vk::PolygonMode::eFill;
    config.rasterizer.cullMode = vk::CullModeFlagBits::eNone; // Visible from both sides
    
    // Blending
    ColorBlendAttachmentState attachment;
    attachment.blendEnable = true;
    attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    attachment.colorBlendOp = vk::BlendOp::eAdd;
    attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    attachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    attachment.alphaBlendOp = vk::BlendOp::eAdd;
    config.colorBlend.attachments.push_back(attachment);
    
    // Depth
    config.depthStencil.depthTestEnable = true;
    config.depthStencil.depthWriteEnable = false;
    config.depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;

    m_pipeline = std::make_unique<VulkanPipeline>();
    m_pipeline->CreateGraphicsPipeline(config);
    
    CreatePlane();
    
    spdlog::info("GravityGridRenderer initialized");
}

void GravityGridRenderer::Shutdown() {
    if (m_Device) {
        vk::Device device = m_Device->GetDevice();
        device.waitIdle();
        
        if (m_DescriptorPool) device.destroyDescriptorPool(m_DescriptorPool);
        if (m_DescriptorSetLayout) device.destroyDescriptorSetLayout(m_DescriptorSetLayout);
    }
    
    if (m_pipeline) m_pipeline->Destroy();
    if (m_shader) m_shader.reset();
    if (m_UBO) m_UBO->Destroy();
    if (m_vbo) m_vbo->Destroy();
    if (m_ebo) m_ebo->Destroy();
}

void GravityGridRenderer::CreatePlane() {
    m_vbo.reset();
    m_ebo.reset();
    m_indexCount = 0;
    
    int N = std::max(2, m_resolution);
    int vertsPerSide = N + 1;
    int vertexCount = vertsPerSide * vertsPerSide;
    
    std::vector<float> vertices;
    vertices.reserve(vertexCount * 3);
    
    float half = m_planeSize * 0.5f;
    for (int z = 0; z <= N; ++z) {
        float tz = (float)z / (float)N;
        float wz = -half + tz * m_planeSize;
        for (int x = 0; x <= N; ++x) {
            float tx = (float)x / (float)N;
            float wx = -half + tx * m_planeSize;
            vertices.push_back(wx);
            vertices.push_back(m_planeY);
            vertices.push_back(wz);
        }
    }
    
    std::vector<uint32_t> indices;
    indices.reserve(N * N * 6);
    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            uint32_t i0 = z * vertsPerSide + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + vertsPerSide;
            uint32_t i3 = i2 + 1;
            
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }
    m_indexCount = (int)indices.size();
    
    m_vbo = std::make_unique<VulkanBuffer>();
    VulkanBufferSpec vboSpec{
        vertices.size() * sizeof(float),
        vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    };
    m_vbo->Create(vboSpec, m_Device);
    m_vbo->Write(vertices.data(), vertices.size() * sizeof(float), 0);
    
    m_ebo = std::make_unique<VulkanBuffer>();
    VulkanBufferSpec eboSpec{
        indices.size() * sizeof(uint32_t),
        vk::BufferUsageFlagBits::eIndexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    };
    m_ebo->Create(eboSpec, m_Device);
    m_ebo->Write(indices.data(), indices.size() * sizeof(uint32_t), 0);
}

void GravityGridRenderer::SetPlaneSize(float size) {
    if (size <= 1.0f) size = 1.0f;
    if (m_planeSize != size) {
        m_planeSize = size;
        CreatePlane();
    }
}

void GravityGridRenderer::SetResolution(int r) {
    r = std::max(8, r);
    if (m_resolution != r) {
        m_resolution = r;
        CreatePlane();
    }
}

void GravityGridRenderer::Render(const Scene& scene, const Camera& camera, float time, vk::CommandBuffer cmd) {
    if (!m_pipeline || !m_pipeline->GetPipeline() || m_indexCount == 0) return;
    
    GravityGridUBO ubo{};
    ubo.vp = camera.GetProjectionMatrix() * camera.GetViewMatrix();
    ubo.color = glm::vec4(m_color, m_opacity);
    ubo.params = glm::vec4(m_planeY, m_cellSize, m_lineThickness, 0.0f);
    
    // Collect objects
    int numBH = 0;
    ParameterHandle posHandle("Entity.Position");
    ParameterHandle bhMassHandle("BlackHole.Mass");
    
    int numSpheres = 0;
    ParameterHandle sphereRadius("Sphere.Radius");
    ParameterHandle sphereMass("Sphere.Mass"); // Assuming mass param exists or default
    
    int numMeshes = 0;
    
    for (const auto& obj : scene.objects) {
        if (obj.HasClass("BlackHole") && numBH < 8) {
             auto pos = obj.GetParameter<glm::vec3>(posHandle).value_or(glm::vec3(0.0f));
             auto mass = obj.GetParameter<float>(bhMassHandle).value_or(1.0f);
             ubo.blackHolePositions[numBH] = glm::vec4(pos, 0.0f);
             ubo.blackHoleMasses[numBH] = glm::vec4(mass); // w = mass
             numBH++;
        }
        else if (obj.HasClass("Sphere") && numSpheres < 16) {
             auto pos = obj.GetParameter<glm::vec3>(posHandle).value_or(glm::vec3(0.0f));
             auto mass = obj.GetParameter<float>(sphereMass).value_or(1.0f);
             ubo.spherePositions[numSpheres] = glm::vec4(pos, 0.0f);
             ubo.sphereMasses[numSpheres] = glm::vec4(mass);
             numSpheres++;
        }
        else if (obj.HasClass("Mesh") && numMeshes < 8) {
             auto pos = obj.GetParameter<glm::vec3>(posHandle).value_or(glm::vec3(0.0f));
             ubo.meshPositions[numMeshes] = glm::vec4(pos, 0.0f);
             ubo.meshMasses[numMeshes] = glm::vec4(1.0f); // Default mass for mesh?
             numMeshes++;
        }
    }
    
    ubo.numBlackHoles = numBH;
    ubo.numSpheres = numSpheres;
    ubo.numMeshes = numMeshes;
    
    m_UBO->Write(&ubo, sizeof(GravityGridUBO));
    
    m_pipeline->Bind(cmd);
    m_pipeline->BindDescriptorSet(cmd, 0, m_DescriptorSet);
    
    vk::Buffer vertexBuffers[] = {m_vbo->GetBuffer()};
    vk::DeviceSize offsets[] = {0};
    cmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
    cmd.bindIndexBuffer(m_ebo->GetBuffer(), 0, vk::IndexType::eUint32);
    
    cmd.drawIndexed(m_indexCount, 1, 0, 0, 0);
}
