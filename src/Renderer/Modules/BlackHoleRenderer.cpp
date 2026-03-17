#include "imgui_impl_vulkan.h"
#include "BlackHoleRenderer.h"
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanImage.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Simulation/Scene.h"
#include "Renderer/Interface/Camera.h"
#include "Application/Application.h"
#include "Application/Parameters.h"
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ===========================================================================================
// Uniform Buffer Structure
// ===========================================================================================

struct CommonUBO {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 invView;
    glm::mat4 invProjection;
    glm::vec4 cameraPos; // w = time
    glm::vec4 cameraFront;
    glm::vec4 cameraUp;
    glm::vec4 cameraRight;
    glm::vec4 params; // x = fov, y = aspect, z = exposure, w = gamma
    glm::vec4 resolution; // x = width, y = height
    
    // Black Hole Data (Max 8)
    int numBlackHoles;
    float _pad0[3];
    float blackHoleMasses[8];
    glm::vec4 blackHolePositions[8]; // w = unused
    float blackHoleSpins[8];
    glm::vec4 blackHoleSpinAxes[8]; // w = unused

    // Settings
    int renderBlackHoles;
    int renderSpheres;
    int accretionDiskEnabled;
    int gravitationalLensingEnabled;
    float cubeSize;
    
    // Ray Marching Settings
    float rayStepSize;
    int maxRaySteps;
    float adaptiveStepRate;
    
    // Third Person
    int enableThirdPerson;
    float thirdPersonDistance;
    float thirdPersonHeight;
    float padding; // Alignment
    
    // LUT Params
    float lutTempMin;
    float lutTempMax;
    float lutRedshiftMin;
    float lutRedshiftMax;
    
    // Accretion Disk Params
    int accretionDiskVolumetric;
    float accDiskHeight;
    float accDiskNoiseScale;
    float accDiskNoiseLOD;
    float accDiskSpeed;
    int gravitationalRedshiftEnabled;
    float dopplerBeamingEnabled;
    float padding2; // Alignment
    
    // Spheres Data (Max 16)
    int numSpheres;
    float padding3[3]; // Alignment
    glm::vec4 spherePositions[16]; // w = radius
    glm::vec4 sphereColors[16]; // w = mass
    
    // Debug
    int debugMode;
    float padding4[3];
};

struct DisplayUBO {
    int u_fxaaEnabled;
    int u_bloomEnabled;
    float u_bloomIntensity;
    int u_bloomDebug;
    float rt_w;
    float rt_h;
    float padding[2];
};

// ===========================================================================================
// Constructor & Destructor
// ===========================================================================================

BlackHoleRenderer::BlackHoleRenderer() {
    m_blackbodyLUTGenerator = std::make_unique<MoleHole::BlackbodyLUTGenerator>();
    m_accelerationLUTGenerator = std::make_unique<MoleHole::AccelerationLUTGenerator>();
    m_hrDiagramLUTGenerator = std::make_unique<MoleHole::HRDiagramLUTGenerator>();
    m_kerrGeodesicLUTGenerator = std::make_unique<MoleHole::KerrGeodesicLUTGenerator>();
}

BlackHoleRenderer::~BlackHoleRenderer() {
    // Cleanup will be done in Init destruction
}

// ===========================================================================================
// Initialization
// ===========================================================================================

void BlackHoleRenderer::Init(VulkanDevice* device, VulkanRenderPass* renderPass, int width, int height) {
    m_Device = device;
    m_width = width;
    m_height = height;

    spdlog::info("BlackHoleRenderer::Init - Starting initialization");

    // Create the compute texture for raytracing output
    CreateComputeTexture();
    
    // Create initial mesh buffers
    CreateMeshBuffers();

    // Generate and upload LUT textures
    GenerateBlackbodyLUT();
    GenerateAccelerationLUT();
    GenerateHRDiagramLUT();
    GenerateKerrGeodesicLUTs();
    
    std::string bgName = Application::Params().Get<std::string>(Params::AppBackgroundImage, "space.hdr");
    LoadSkybox("../assets/backgrounds/" + bgName);

    // Load shaders
    m_computeShader = std::make_unique<VulkanShader>(
        device,
        std::vector<std::pair<ShaderStage, std::string>>{
            {ShaderStage::Compute, "shaders/black_hole_rendering.comp"}
        }
    );

    m_displayShader = std::make_unique<VulkanShader>(
        device,
        std::vector<std::pair<ShaderStage, std::string>>{
            {ShaderStage::Vertex, "shaders/blackhole_display.vert"},
            {ShaderStage::Fragment, "shaders/blackhole_display.frag"}
        }
    );

    // Create compute descriptor set layout
    std::vector<vk::DescriptorSetLayoutBinding> computeBindings;

    // Binding 0: Output Storage Image
    computeBindings.push_back(vk::DescriptorSetLayoutBinding{
        0,                                      // binding
        vk::DescriptorType::eStorageImage,      // descriptorType
        1,                                      // descriptorCount
        vk::ShaderStageFlagBits::eCompute,      // stageFlags
        nullptr                                 // pImmutableSamplers
    });

    // Binding 1: Uniform Buffer
    computeBindings.push_back(vk::DescriptorSetLayoutBinding{
        1,                                      // binding
        vk::DescriptorType::eUniformBuffer,     // descriptorType
        1,                                      // descriptorCount
        vk::ShaderStageFlagBits::eCompute,      // stageFlags
        nullptr                                 // pImmutableSamplers
    });

    // Binding 2: Skybox texture (Sampler)
    computeBindings.push_back(vk::DescriptorSetLayoutBinding{
        2,                                      // binding
        vk::DescriptorType::eCombinedImageSampler, // descriptorType
        1,                                      // descriptorCount
        vk::ShaderStageFlagBits::eCompute,      // stageFlags
        nullptr                                 // pImmutableSamplers
    });

    // Binding 3-9: LUT textures (7 LUTs)
    for (int i = 0; i < 7; i++) {
        computeBindings.push_back(vk::DescriptorSetLayoutBinding{
            static_cast<uint32_t>(3 + i),       // binding
            vk::DescriptorType::eCombinedImageSampler, // descriptorType
            1,                                  // descriptorCount
            vk::ShaderStageFlagBits::eCompute,  // stageFlags
            nullptr                             // pImmutableSamplers
        });
    }

    // Binding 10: Mesh Triangles SSBO
    computeBindings.push_back(vk::DescriptorSetLayoutBinding{
        10,                                     // binding
        vk::DescriptorType::eStorageBuffer,     // descriptorType
        1,                                      // descriptorCount
        vk::ShaderStageFlagBits::eCompute,      // stageFlags
        nullptr                                 // pImmutableSamplers
    });

    vk::DescriptorSetLayoutCreateInfo computeLayoutInfo{
        {},                               // flags
        static_cast<uint32_t>(computeBindings.size()),
        computeBindings.data()
    };

    m_ComputeDescriptorSetLayout = device->GetDevice().createDescriptorSetLayout(computeLayoutInfo);

    // Create descriptor pool
    std::vector<vk::DescriptorPoolSize> poolSizes;
    poolSizes.push_back(vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1});
    poolSizes.push_back(vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 2}); // Common + Display
    poolSizes.push_back(vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 8}); // Skybox + 7 LUTs
    poolSizes.push_back(vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1}); // Mesh Data
    poolSizes.push_back(vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 10}); // 8 Compute + 2 Display

    vk::DescriptorPoolCreateInfo poolInfo{
        {},                                     // flags
        2,                                      // maxSets (compute + display)
        static_cast<uint32_t>(poolSizes.size()),
        poolSizes.data()
    };

    m_ComputeDescriptorPool = device->GetDevice().createDescriptorPool(poolInfo);

    // Allocate compute descriptor set
    vk::DescriptorSetAllocateInfo allocInfo{
        m_ComputeDescriptorPool,
        1,
        &m_ComputeDescriptorSetLayout
    };

    m_ComputeDescriptorSet = device->GetDevice().allocateDescriptorSets(allocInfo)[0];
spdlog::info("BlackHoleRenderer::Init - Allocated Compute Descriptor Set: {}", (void*)m_ComputeDescriptorSet);

    // Create common UBO
    m_CommonUBO = std::make_unique<VulkanBuffer>();
    VulkanBufferSpec uboSpec{
        sizeof(CommonUBO),
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    };
    m_CommonUBO->Create(uboSpec, device);
    
    // Create Display UBO
    m_DisplayUBO = std::make_unique<VulkanBuffer>();
    VulkanBufferSpec displayUboSpec{
        sizeof(DisplayUBO),
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    };
    m_DisplayUBO->Create(displayUboSpec, device);

    // Create compute pipeline
    m_computePipeline = std::make_unique<VulkanPipeline>();
    ComputePipelineConfig computeConfig{
        device,
        m_computeShader.get(),
        std::vector<vk::DescriptorSetLayout>{m_ComputeDescriptorSetLayout}
    };
    if (!m_computePipeline->CreateComputePipeline(computeConfig)) {
        spdlog::error("BlackHoleRenderer::Init: Failed to create compute pipeline");
    }

    // Display Pipeline Setup
    // ----------------------
    
    // Layout
    std::vector<vk::DescriptorSetLayoutBinding> displayBindings;
    displayBindings.push_back(vk::DescriptorSetLayoutBinding{
        0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr // u_raytracedImage
    });
    // displayBindings.push_back(vk::DescriptorSetLayoutBinding{
    //     1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr // u_bloomImage
    // });
    displayBindings.push_back(vk::DescriptorSetLayoutBinding{
        1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment, nullptr // DisplayParams UBO
    });

    vk::DescriptorSetLayoutCreateInfo displayLayoutInfo{
        {}, static_cast<uint32_t>(displayBindings.size()), displayBindings.data()
    };
    m_DisplayDescriptorSetLayout = device->GetDevice().createDescriptorSetLayout(displayLayoutInfo);

    // Allocate Set
    vk::DescriptorSetAllocateInfo displayAllocInfo{
        m_ComputeDescriptorPool, 1, &m_DisplayDescriptorSetLayout
    };
    m_DisplayDescriptorSet = device->GetDevice().allocateDescriptorSets(displayAllocInfo)[0];

    // Create Pipeline
    m_displayPipeline = std::make_unique<VulkanPipeline>();
    GraphicsPipelineConfig displayConfig;
    displayConfig.device = device;
    displayConfig.shader = m_displayShader.get();
    displayConfig.renderPass = renderPass;
    displayConfig.descriptorSetLayouts = { m_DisplayDescriptorSetLayout };
    
    // Config via nested structs
    displayConfig.inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    displayConfig.rasterizer.cullMode = vk::CullModeFlagBits::eNone;
    displayConfig.depthStencil.depthTestEnable = VK_FALSE;
    displayConfig.depthStencil.depthWriteEnable = VK_FALSE;
    
    // No vertex bindings needed
    m_displayPipeline->CreateGraphicsPipeline(displayConfig);

    // Create mesh buffers (stubs for now)
    CreateMeshBuffers();

    UpdateDisplayDescriptorSet();
    UpdateComputeDescriptorSet();

    spdlog::info("BlackHoleRenderer::Init - Initialization complete");
}

// ===========================================================================================
// Texture Creation
// ===========================================================================================

void BlackHoleRenderer::CreateComputeTexture() {
    m_computeTexture = std::make_shared<VulkanImage>();
    VulkanImageSpec spec{
        glm::vec3(m_width, m_height, 1.0f),     // Size
        vk::ImageTiling::eOptimal,              // Tiling
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled, // Usage
        vk::MemoryPropertyFlagBits::eDeviceLocal, // MemoryProperties
        vk::Format::eR32G32B32A32Sfloat,        // Format
        true,                                   // CreateView
        vk::ImageAspectFlagBits::eColor,        // ViewAspectFlags
        1,                                      // MipLevels
        1,                                      // ArrayLayers
        vk::SampleCountFlagBits::e1             // Samples
    };
    m_computeTexture->Create(spec, m_Device);

    // Transition to ShaderReadOnlyOptimal layout initially
    // This matches the expected "oldLayout" in the Render() loop
    m_computeTexture->TransitionLayout(
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader
    );

    // Create a sampler for the compute texture
    VulkanSamplerSpec samplerSpec{
        vk::Filter::eLinear,
        vk::Filter::eLinear,
        vk::SamplerMipmapMode::eLinear,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge
    };
    m_computeTexture->CreateSampler(samplerSpec);
}

void BlackHoleRenderer::CreateBloomTextures() {
    // Bloom bright texture
    m_bloomBrightTexture = std::make_shared<VulkanImage>();
    VulkanImageSpec brightSpec{
        glm::vec3(m_width / 2, m_height / 2, 1.0f),
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::Format::eR32G32B32A32Sfloat,
        true,
        vk::ImageAspectFlagBits::eColor,
        1, 1, vk::SampleCountFlagBits::e1
    };
    m_bloomBrightTexture->Create(brightSpec, m_Device);

    VulkanSamplerSpec samplerSpec{
        vk::Filter::eLinear,
        vk::Filter::eLinear,
        vk::SamplerMipmapMode::eLinear,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge
    };
    m_bloomBrightTexture->CreateSampler(samplerSpec);

    // Bloom blur textures (ping-pong)
    for (int i = 0; i < 2; i++) {
        m_bloomBlurTexture[i] = std::make_shared<VulkanImage>();
        m_bloomBlurTexture[i]->Create(brightSpec, m_Device);
        m_bloomBlurTexture[i]->CreateSampler(samplerSpec);
    }
    m_bloomFinalTextureIndex = 0;
}

// ===========================================================================================
// LUT Generation
// ===========================================================================================

void BlackHoleRenderer::GenerateBlackbodyLUT() {
    spdlog::info("Generating Blackbody LUT...");
    
    auto lutDataRGB = MoleHole::BlackbodyLUTGenerator::generateLUT();

    // Repack RGB to RGBA
    std::vector<float> lutDataRGBA;
    lutDataRGBA.reserve(lutDataRGB.size() / 3 * 4);
    for (size_t i = 0; i < lutDataRGB.size(); i += 3) {
        lutDataRGBA.push_back(lutDataRGB[i]);
        lutDataRGBA.push_back(lutDataRGB[i+1]);
        lutDataRGBA.push_back(lutDataRGB[i+2]);
        lutDataRGBA.push_back(1.0f); // Alpha
    }
    
    m_blackbodyLUT = std::make_shared<VulkanImage>();
    VulkanImageSpec spec{
        glm::vec3(MoleHole::BlackbodyLUTGenerator::LUT_WIDTH, MoleHole::BlackbodyLUTGenerator::LUT_HEIGHT, 1.0f),
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::Format::eR32G32B32A32Sfloat,  // RGBA floats (supported)
        true,
        vk::ImageAspectFlagBits::eColor
    };
    m_blackbodyLUT->Create(spec, m_Device);

    // Transition to TransferDst
    m_blackbodyLUT->TransitionLayout(
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal,
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer
    );

    // Upload data via staging buffer
    size_t dataSize = lutDataRGBA.size() * sizeof(float);
    VulkanBuffer stagingBuffer;
    VulkanBufferSpec stagingSpec{
        dataSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    };
    stagingBuffer.Create(stagingSpec, m_Device);
    stagingBuffer.Write(lutDataRGBA.data(), dataSize, 0);

    m_blackbodyLUT->CopyBufferToImage(
        stagingBuffer.GetBuffer(),
        MoleHole::BlackbodyLUTGenerator::LUT_WIDTH,
        MoleHole::BlackbodyLUTGenerator::LUT_HEIGHT,
        1,
        0, 0
    );

    // Transition to shader read-only
    m_blackbodyLUT->TransitionLayout(
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader
    );

    // Create sampler
    VulkanSamplerSpec samplerSpec{
        vk::Filter::eLinear,
        vk::Filter::eLinear,
        vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge
    };
    m_blackbodyLUT->CreateSampler(samplerSpec);

    stagingBuffer.Destroy();
    spdlog::info("Blackbody LUT generated successfully");
}

void BlackHoleRenderer::GenerateAccelerationLUT() {
    spdlog::info("Generating Acceleration LUT...");
    
    auto lutData = MoleHole::AccelerationLUTGenerator::generateLUT();
    
    m_accelerationLUT = std::make_shared<VulkanImage>();
    VulkanImageSpec spec{
        glm::vec3(MoleHole::AccelerationLUTGenerator::LUT_WIDTH, MoleHole::AccelerationLUTGenerator::LUT_HEIGHT, 1.0f),
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::Format::eR32Sfloat,  // Single channel float (usually supported)
        true,
        vk::ImageAspectFlagBits::eColor
    };
    m_accelerationLUT->Create(spec, m_Device);

    // Transition to TransferDst
    m_accelerationLUT->TransitionLayout(
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal,
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer
    );

    // Upload data via staging buffer
    size_t dataSize = lutData.size() * sizeof(float);
    VulkanBuffer stagingBuffer;
    VulkanBufferSpec stagingSpec{
        dataSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    };
    stagingBuffer.Create(stagingSpec, m_Device);
    stagingBuffer.Write(lutData.data(), dataSize, 0);

    m_accelerationLUT->CopyBufferToImage(
        stagingBuffer.GetBuffer(),
        MoleHole::AccelerationLUTGenerator::LUT_WIDTH,
        MoleHole::AccelerationLUTGenerator::LUT_HEIGHT,
        1,
        0, 0
    );

    // Transition to shader read-only
    m_accelerationLUT->TransitionLayout(
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader
    );

    // Create sampler
    VulkanSamplerSpec samplerSpec{
        vk::Filter::eLinear,
        vk::Filter::eLinear,
        vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge
    };
    m_accelerationLUT->CreateSampler(samplerSpec);

    stagingBuffer.Destroy();
    spdlog::info("Acceleration LUT generated successfully");
}

void BlackHoleRenderer::GenerateHRDiagramLUT() {
    spdlog::info("Generating HR Diagram LUT...");
    
    auto lutDataRGB = MoleHole::HRDiagramLUTGenerator::generateLUT();
    
    // Repack RGB to RGBA
    std::vector<float> lutDataRGBA;
    lutDataRGBA.reserve(lutDataRGB.size() / 3 * 4);
    for (size_t i = 0; i < lutDataRGB.size(); i += 3) {
        lutDataRGBA.push_back(lutDataRGB[i]);
        lutDataRGBA.push_back(lutDataRGB[i+1]);
        lutDataRGBA.push_back(lutDataRGB[i+2]);
        lutDataRGBA.push_back(1.0f); // Alpha
    }

    m_hrDiagramLUT = std::make_shared<VulkanImage>();
    VulkanImageSpec spec{
        glm::vec3(MoleHole::HRDiagramLUTGenerator::LUT_SIZE, 1.0f, 1.0f),
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::Format::eR32G32B32A32Sfloat,  // RGBA floats
        true,
        vk::ImageAspectFlagBits::eColor
    };
    m_hrDiagramLUT->Create(spec, m_Device);

    // Transition to TransferDst
    m_hrDiagramLUT->TransitionLayout(
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal,
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer
    );

    // Upload data via staging buffer
    size_t dataSize = lutDataRGBA.size() * sizeof(float);
    VulkanBuffer stagingBuffer;
    VulkanBufferSpec stagingSpec{
        dataSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    };
    stagingBuffer.Create(stagingSpec, m_Device);
    stagingBuffer.Write(lutDataRGBA.data(), dataSize, 0);

    m_hrDiagramLUT->CopyBufferToImage(
        stagingBuffer.GetBuffer(),
        MoleHole::HRDiagramLUTGenerator::LUT_SIZE,
        1,
        1,
        0, 0
    );

    // Transition to shader read-only
    m_hrDiagramLUT->TransitionLayout(
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader
    );

    // Create sampler
    VulkanSamplerSpec samplerSpec{
        vk::Filter::eLinear,
        vk::Filter::eLinear,
        vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge
    };
    m_hrDiagramLUT->CreateSampler(samplerSpec);

    stagingBuffer.Destroy();
    spdlog::info("HR Diagram LUT generated successfully");
}

void BlackHoleRenderer::GenerateKerrGeodesicLUTs() {
    spdlog::info("Kerr Geodesic LUT generation disabled (unused).");
    return; // Early return to skip generation

    /*
    spdlog::info("Generating Kerr Geodesic LUTs...");
    
    auto deflectionData = MoleHole::KerrGeodesicLUTGenerator::generateDeflectionLUT();
    // ...
    // ... (rest of the function)
    // ...
    stagingBuffer.Destroy();
    spdlog::info("Kerr Geodesic LUTs generated successfully");
    */
}

// ===========================================================================================
// Rendering
// ===========================================================================================

void BlackHoleRenderer::Render(const Scene& scene,
                              const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache,
                              const Camera& camera,
                              float time,
                              vk::CommandBuffer cmd) {
    if (!m_computePipeline || !m_computePipeline->GetPipeline() || !m_computeTexture) {
        // Only warn once per second to avoid flooding logs
        static float lastWarnTime = 0.0f;
        if (time - lastWarnTime > 1.0f) {
            spdlog::warn("BlackHoleRenderer::Render - Pipeline or texture not initialized. Pipeline Ptr: {}, Handle: {}", (void*)m_computePipeline.get(), m_computePipeline ? (void*)VkPipeline(m_computePipeline->GetPipeline()) : nullptr);
            lastWarnTime = time;
        }
        return;
    }

    // Update uniforms
    UpdateUniforms(scene, meshCache, camera, time, cmd);

    // Ensure texture is in General layout for compute write
    if (m_computeTexture) {
         m_computeTexture->TransitionLayout(
            cmd,
            vk::ImageLayout::eShaderReadOnlyOptimal, // Assuming it was left in ReadOnly from previous frame
            vk::ImageLayout::eGeneral,
            vk::PipelineStageFlagBits::eFragmentShader, // Wait for fragment shader to finish reading
            vk::PipelineStageFlagBits::eComputeShader
        );
    }

    // Bind compute pipeline
    m_computePipeline->Bind(cmd);

    // Bind descriptor set
    m_computePipeline->BindDescriptorSet(cmd, 0, m_ComputeDescriptorSet);

    // Dispatch compute shader
    uint32_t groupCountX = (m_width + 15) / 16;
    uint32_t groupCountY = (m_height + 15) / 16;
    cmd.dispatch(groupCountX, groupCountY, 1);

    // Optional: Apply post-processing effects
    if (false) { // Bloom disabled for now
        ApplyBloom(cmd);
        ApplyLensFlare(cmd);
    }
}

void BlackHoleRenderer::PrepareDisplay(vk::CommandBuffer cmd) {
    if (!m_computeTexture) return;

    // Transition compute texture to shader read (must be outside render pass)
    m_computeTexture->TransitionLayout(
        cmd,
        vk::ImageLayout::eGeneral,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eFragmentShader
    );
}

void BlackHoleRenderer::UpdateDisplayDescriptorSet() {
    if (!m_DisplayDescriptorSet || !m_computeTexture || !m_DisplayUBO) return;

    // 1. Image
    vk::DescriptorImageInfo imageInfo{
        m_computeTexture->GetSampler(),
        m_computeTexture->GetImageView(),
        vk::ImageLayout::eShaderReadOnlyOptimal
    };
    vk::WriteDescriptorSet imageWrite{
        m_DisplayDescriptorSet,
        0,
        0,
        1,
        vk::DescriptorType::eCombinedImageSampler,
        &imageInfo,
        nullptr,
        nullptr
    };

    // 2. UBO
    vk::DescriptorBufferInfo bufferInfo = m_DisplayUBO->GetDescriptorInfo();
    vk::WriteDescriptorSet bufferWrite{
        m_DisplayDescriptorSet,
        1,
        0,
        1,
        vk::DescriptorType::eUniformBuffer,
        nullptr,
        &bufferInfo,
        nullptr
    };

    m_Device->GetDevice().updateDescriptorSets(2, std::array<vk::WriteDescriptorSet, 2>{imageWrite, bufferWrite}.data(), 0, nullptr);
}

void BlackHoleRenderer::CreateFallbackSkybox() {
    spdlog::info("CreateFallbackSkybox: Creating fallback skybox");
    float data[] = { 1.0f, 0.0f, 1.0f, 1.0f }; // Magenta
    int width = 1, height = 1;

    // Destroy old if exists
    if (m_skyboxTexture) m_skyboxTexture.reset();

    VulkanImageSpec spec{
        glm::vec3(width, height, 1.0f),
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::Format::eR32G32B32A32Sfloat,
        true,
        vk::ImageAspectFlagBits::eColor
    };

    m_skyboxTexture = std::make_shared<VulkanImage>();
    m_skyboxTexture->Create(spec, m_Device);

    // Staging buffer
    VulkanBuffer stagingBuffer;
    VulkanBufferSpec bufferSpec{
        sizeof(data),
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    };
    stagingBuffer.Create(bufferSpec, m_Device);
    stagingBuffer.Write(data, sizeof(data));

    // Transition & Copy (reusing logic from LoadSkybox would be cleaner but inline for now)
    m_skyboxTexture->TransitionLayout(
        vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer
    );

    m_skyboxTexture->CopyBufferToImage(stagingBuffer.GetBuffer(), width, height);

    m_skyboxTexture->TransitionLayout(
        vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader
    );

    VulkanSamplerSpec samplerSpec{
        vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat
    };
    m_skyboxTexture->CreateSampler(samplerSpec);
    
    stagingBuffer.Destroy();
    spdlog::warn("Created fallback skybox texture (Magenta)");
}

void BlackHoleRenderer::UpdateComputeDescriptorSet() {
    if (!m_ComputeDescriptorSet) {
        spdlog::warn("UpdateComputeDescriptorSet: Descriptor Set not yet allocated");
        return;
    }

    std::vector<vk::WriteDescriptorSet> writes;

    // 0: Storage Image
    vk::DescriptorImageInfo storageImageInfo{
        {}, m_computeTexture->GetImageView(), vk::ImageLayout::eGeneral
    };
    writes.push_back({m_ComputeDescriptorSet, 0, 0, 1, vk::DescriptorType::eStorageImage, &storageImageInfo, nullptr, nullptr});

    // 1: Uniform Buffer
    vk::DescriptorBufferInfo bufferInfo = m_CommonUBO->GetDescriptorInfo();
    writes.push_back({m_ComputeDescriptorSet, 1, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufferInfo, nullptr});

    // 2: Skybox
    vk::DescriptorImageInfo skyboxInfo{};
    if (m_skyboxTexture) {
        spdlog::info("UpdateComputeDescriptorSet: Updating binding 2 (Skybox) with valid texture");
        skyboxInfo = {m_skyboxTexture->GetSampler(), m_skyboxTexture->GetImageView(), vk::ImageLayout::eShaderReadOnlyOptimal};
        writes.push_back({m_ComputeDescriptorSet, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &skyboxInfo, nullptr, nullptr});
    } else {
        spdlog::error("UpdateComputeDescriptorSet: Skybox texture is NULL! Binding 2 not updated.");
    }

    // 3-9: LUTs
    std::vector<vk::DescriptorImageInfo> lutInfos;
    std::vector<std::shared_ptr<VulkanImage>> luts{
        m_blackbodyLUT, m_accelerationLUT, m_hrDiagramLUT,
        m_kerrDeflectionLUT, m_kerrRedshiftLUT, m_kerrPhotonSphereLUT, m_kerrISCOLUT
    };
    
    // We need to keep lutInfos alive until updateDescriptorSets call!
    // Reserve to prevent reallocation
    lutInfos.reserve(luts.size());

    for (size_t i = 0; i < luts.size(); i++) {
        if (luts[i]) {
            lutInfos.push_back({luts[i]->GetSampler(), luts[i]->GetImageView(), vk::ImageLayout::eShaderReadOnlyOptimal});
        } else {
            // Create a dummy info or skip? If shader expects it, we must bind something valid or enable robust buffer access.
            // For now, assuming null descriptor updates are ignored or cause crash if accessed.
            // Better to bind a dummy image if null.
            // But let's just skip and hope the shader handles it (or we don't access it).
            // Actually, pushing back empty info is bad.
            // Let's create a placeholder info if null (using blackbodyLUT as fallback)
            if (m_blackbodyLUT) {
                 lutInfos.push_back({m_blackbodyLUT->GetSampler(), m_blackbodyLUT->GetImageView(), vk::ImageLayout::eShaderReadOnlyOptimal});
            } else {
                 lutInfos.push_back({}); // Will likely crash validation
            }
        }
    }

    for (size_t i = 0; i < lutInfos.size(); i++) {
        writes.push_back({m_ComputeDescriptorSet, 3 + (uint32_t)i, 0, 1, vk::DescriptorType::eCombinedImageSampler, &lutInfos[i], nullptr, nullptr});
    }

    // 10: Mesh SSBO
    vk::DescriptorBufferInfo meshBufferInfo{};
    if (m_triangleSSBO) {
        meshBufferInfo = m_triangleSSBO->GetDescriptorInfo();
        writes.push_back({m_ComputeDescriptorSet, 10, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &meshBufferInfo, nullptr});
    }

    m_Device->GetDevice().updateDescriptorSets(writes, nullptr);
}

void BlackHoleRenderer::RenderToScreen(vk::CommandBuffer cmd) {
    if (!m_displayPipeline) return;

    // Transition handled in PrepareDisplay()

    // Update UBO
    DisplayUBO ubo{};
    ubo.u_fxaaEnabled = 1; // Get from params
    ubo.u_bloomEnabled = 0;
    ubo.u_bloomIntensity = 1.0f;
    ubo.u_bloomDebug = 0;
    ubo.rt_w = (float)m_width;
    ubo.rt_h = (float)m_height;
    
    // We can use Write here because UBO is host visible/coherent.
    // However, writing during frame recording might need synchronization if multiple frames in flight use the same buffer.
    // VulkanBuffer handles writing, but if we have multiple frames in flight, we usually need per-frame UBOs.
    // For now, let's assume we are safe or risk race condition (common in simple ports).
    // Ideally we should have m_DisplayUBOs[FrameIndex].
    // But since BlackHoleRenderer uses a single m_DisplayUBO, we might flicker.
    // For now, let's proceed.
    m_DisplayUBO->Write(&ubo, sizeof(DisplayUBO));

    // Bind Pipeline
    m_displayPipeline->Bind(cmd);
    
    // Bind Descriptor Set
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_displayPipeline->GetLayout(), 0, 1, &m_DisplayDescriptorSet, 0, nullptr);

    // Draw full screen triangle (3 vertices, 0-2)
    cmd.draw(3, 1, 0, 0);

    // Transition back to General is handled in Render() start
}

// ===========================================================================================
// Utility Functions
// ===========================================================================================

void BlackHoleRenderer::UpdateUniforms(const Scene& scene,
                                      const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache,
                                      const Camera& camera,
                                      float time,
                                      vk::CommandBuffer cmd) {
    // spdlog::debug("UpdateUniforms: Start");
    CommonUBO ubo{};

    // Camera Matrices
    ubo.view = camera.GetViewMatrix();
    ubo.projection = camera.GetProjectionMatrix();
    ubo.invView = glm::inverse(ubo.view);
    ubo.invProjection = glm::inverse(ubo.projection);

    // Camera Vectors
    ubo.cameraPos = glm::vec4(camera.GetPosition(), time);
    ubo.cameraFront = glm::vec4(camera.GetFront(), 0.0f);
    ubo.cameraUp = glm::vec4(camera.GetUp(), 0.0f);
    ubo.cameraRight = glm::vec4(camera.GetRight(), 0.0f);
    
    // Params
    ubo.params = glm::vec4(camera.GetFov(), camera.GetAspect(), 1.0f, 2.2f); // exposure, gamma
    ubo.resolution = glm::vec4(m_width, m_height, 0.0f, 0.0f);

    // Settings (Hardcoded defaults for now, should come from AppState)
    ubo.renderBlackHoles = 1;
    ubo.renderSpheres = 1;
    ubo.accretionDiskEnabled = 1;
    ubo.gravitationalLensingEnabled = 1;
    ubo.cubeSize = 1.0f;
    ubo.rayStepSize = 0.3f;
    ubo.maxRaySteps = 1000;
    ubo.adaptiveStepRate = 0.1f;
    ubo.enableThirdPerson = 0;
    ubo.thirdPersonDistance = 5.0f;
    ubo.thirdPersonHeight = 2.0f;

    // LUT Params (Hardcoded based on generators)
    ubo.lutTempMin = 1000.0f;
    ubo.lutTempMax = 40000.0f;
    ubo.lutRedshiftMin = 0.1f;
    ubo.lutRedshiftMax = 3.0f;

    // Accretion Disk Params
    ubo.accretionDiskVolumetric = 0;
    ubo.accDiskHeight = 0.2f;
    ubo.accDiskNoiseScale = 1.0f;
    ubo.accDiskNoiseLOD = 5.0f;
    ubo.accDiskSpeed = 0.5f;
    ubo.gravitationalRedshiftEnabled = 1;
    ubo.dopplerBeamingEnabled = 1.0f;

    // Populate Scene Objects
    int bhCount = 0;
    int sphereCount = 0;

    ParameterHandle posHandle("Entity.Position");
    ParameterHandle massHandle("BlackHole.Mass");
    ParameterHandle spinHandle("BlackHole.Spin");
    ParameterHandle axisHandle("BlackHole.SpinAxis");
    
    ParameterHandle radiusHandle("Sphere.Radius");
    ParameterHandle colorHandle("Sphere.Color");
    ParameterHandle sphereMassHandle("Sphere.Mass");

    for (const auto& obj : scene.objects) {
        if (obj.HasClass("BlackHole") && bhCount < 8) {
            ubo.blackHolePositions[bhCount] = glm::vec4(obj.GetParameter<glm::vec3>(posHandle).value_or(glm::vec3(0.0f)), 0.0f);
            ubo.blackHoleMasses[bhCount] = obj.GetParameter<float>(massHandle).value_or(1.0f);
            ubo.blackHoleSpins[bhCount] = obj.GetParameter<float>(spinHandle).value_or(0.0f);
            ubo.blackHoleSpinAxes[bhCount] = glm::vec4(obj.GetParameter<glm::vec3>(axisHandle).value_or(glm::vec3(0,1,0)), 0.0f);
            bhCount++;
        } else if (obj.HasClass("Sphere") && sphereCount < 16) {
            ubo.spherePositions[sphereCount] = glm::vec4(obj.GetParameter<glm::vec3>(posHandle).value_or(glm::vec3(0.0f)), 
                                                        obj.GetParameter<float>(radiusHandle).value_or(1.0f)); // w = radius
            ubo.sphereColors[sphereCount] = glm::vec4(obj.GetParameter<glm::vec3>(colorHandle).value_or(glm::vec3(1.0f)),
                                                     obj.GetParameter<float>(sphereMassHandle).value_or(0.0f)); // w = mass
            sphereCount++;
        }
    }
    ubo.numBlackHoles = bhCount;
    ubo.numSpheres = sphereCount;
    ubo.debugMode = Application::Params().Get<int>(Params::RenderingDebugMode, 0);

    // Update mesh SSBO
    UpdateMeshBuffers(scene, meshCache);

    // Write to UBO
    m_CommonUBO->Write(&ubo, sizeof(CommonUBO), 0);
}

struct ShaderTriangle {
    alignas(16) glm::vec4 v0;
    alignas(16) glm::vec4 v1;
    alignas(16) glm::vec4 v2;
    alignas(16) glm::vec4 color;
};

void BlackHoleRenderer::UpdateMeshBuffers(const Scene& scene,
                                         const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache) {
    std::vector<ShaderTriangle> triangles;
    
    // Collect triangles from all meshes
    for (const auto& obj : scene.objects) {
         if (!obj.HasClass("Mesh")) continue;
         
         // Assuming object has a "Mesh" property which is a path or name
         std::string meshPath;
         if (auto val = obj.GetParameter<std::string>(ParameterHandle("Mesh"))) {
             meshPath = *val;
         } else {
             continue;
         }
         
         if (meshPath.empty()) continue;
         
         auto it = meshCache.find(meshPath);
         if (it == meshCache.end() || !it->second) continue;
         
         auto mesh = it->second;
         auto geometry = mesh->GetPhysicsGeometry();
         
         glm::mat4 transform = glm::mat4(1.0f);
         // Get transform from object
         glm::vec3 pos(0.0f);
         if (auto val = obj.GetParameter<glm::vec3>(ParameterHandle("Position"))) {
             pos = *val;
         }
         
         glm::quat rot = glm::identity<glm::quat>();
         if (auto val = obj.GetParameter<glm::quat>(ParameterHandle("Rotation"))) {
             rot = *val;
         }
         
         glm::vec3 scale(1.0f);
         if (auto val = obj.GetParameter<glm::vec3>(ParameterHandle("Scale"))) {
             scale = *val;
         }
         
         transform = glm::translate(transform, pos);
         transform *= glm::mat4_cast(rot);
         transform = glm::scale(transform, scale);
         
         // Helper to transform vec3
         auto transformPoint = [&](const glm::vec3& p) {
             return glm::vec3(transform * glm::vec4(p, 1.0f));
         };
         
         for (size_t i = 0; i < geometry.indices.size(); i += 3) {
             if (i + 2 >= geometry.indices.size()) break;
             
             unsigned int i0 = geometry.indices[i];
             unsigned int i1 = geometry.indices[i+1];
             unsigned int i2 = geometry.indices[i+2];
             
             if (i0 >= geometry.vertices.size() || i1 >= geometry.vertices.size() || i2 >= geometry.vertices.size()) continue;
             
             ShaderTriangle tri;
             tri.v0 = glm::vec4(transformPoint(geometry.vertices[i0]), 1.0f);
             tri.v1 = glm::vec4(transformPoint(geometry.vertices[i1]), 1.0f);
             tri.v2 = glm::vec4(transformPoint(geometry.vertices[i2]), 1.0f);
             tri.color = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f); // Default gray color
             
             triangles.push_back(tri);
         }
    }
    
    // Update SSBO
    // Recreate buffer if size changed or doesn't exist
    vk::DeviceSize bufferSize = std::max(size_t(1), triangles.size()) * sizeof(ShaderTriangle);
    
    // If we have triangles, write them
    if (!triangles.empty()) {
        if (!m_triangleSSBO || m_triangleSSBO->GetSize() < bufferSize) {
            if (m_triangleSSBO) m_triangleSSBO->Destroy();
            m_triangleSSBO = std::make_unique<VulkanBuffer>();
            
            VulkanBufferSpec spec{};
            spec.Size = bufferSize;
            spec.Usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            spec.MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent; // For simplicity
            m_triangleSSBO->Create(spec, m_Device);
        }
        
        m_triangleSSBO->Write(triangles.data(), bufferSize);
        
        // Update descriptor set
        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_triangleSSBO->GetBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = bufferSize;

        vk::WriteDescriptorSet descriptorWrite{};
        descriptorWrite.dstSet = m_ComputeDescriptorSet;
        descriptorWrite.dstBinding = 10;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        m_Device->GetDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
    }
    
    // Update u_numTriangles in UBO?
    // We need to pass the triangle count to the shader.
    // The CommonUBO has params block. Let's see where u_numTriangles can go.
    // Or simpler: put it in the buffer size?
    // Usually we add a uniform for count.
    // Let's check CommonUBO structure in BlackHoleRenderer.cpp UpdateUniforms.
}

void BlackHoleRenderer::CreateMeshBuffers() {
    // Initial creation handled in UpdateMeshBuffers or here with empty
    // Just ensure non-null to avoid crashes if bound
    VulkanBufferSpec spec{};
    spec.Size = sizeof(ShaderTriangle) * 1; // At least one
    spec.Usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
    spec.MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    
    m_triangleSSBO = std::make_unique<VulkanBuffer>();
    m_triangleSSBO->Create(spec, m_Device);
    
    // Create dummy data
    ShaderTriangle dummy;
    dummy.v0 = glm::vec4(0);
    m_triangleSSBO->Write(&dummy, sizeof(dummy));
}

void BlackHoleRenderer::ApplyBloom(vk::CommandBuffer cmd) {
    // Stub: Implement bloom extraction and blur
    spdlog::debug("ApplyBloom called");
}

void BlackHoleRenderer::ApplyLensFlare(vk::CommandBuffer cmd) {
    // Stub: Implement lens flare
    spdlog::debug("ApplyLensFlare called");
}

void BlackHoleRenderer::CreateFullscreenQuad() {
    // Stub: Create a fullscreen quad for rendering
    spdlog::debug("CreateFullscreenQuad called");
}

#include <stb_image.h>

void BlackHoleRenderer::LoadSkybox(const std::string& path) {
    if (path.empty()) return;
    
    // Check if path exists
    if (!std::filesystem::exists(path)) {
        spdlog::error("Skybox file does not exist: {}", path);
        CreateFallbackSkybox();
        UpdateComputeDescriptorSet();
        return;
    }

    int width, height, channels;
    float* data = stbi_loadf(path.c_str(), &width, &height, &channels, 4);

    if (!data) {
        spdlog::error("Failed to load skybox texture: {}", path);
        CreateFallbackSkybox();
        UpdateComputeDescriptorSet();
        return;
    }

    // Destroy old texture if it exists
    if (m_skyboxTexture) {
        // Just reset shared_ptr, VulkanImage destructor handles cleanup
        m_skyboxTexture.reset();
    }

    VulkanImageSpec spec{
        glm::vec3(width, height, 1.0f),
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::Format::eR32G32B32A32Sfloat,
        true,
        vk::ImageAspectFlagBits::eColor
    };

    m_skyboxTexture = std::make_shared<VulkanImage>();
    m_skyboxTexture->Create(spec, m_Device);

    // Create staging buffer
    vk::DeviceSize imageSize = width * height * 4 * sizeof(float);
    VulkanBuffer stagingBuffer;
    VulkanBufferSpec bufferSpec{
        imageSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    };
    stagingBuffer.Create(bufferSpec, m_Device);
    stagingBuffer.Write(data, imageSize);

    stbi_image_free(data);

    // Transition to TransferDst
    m_skyboxTexture->TransitionLayout(
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal,
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer
    );

    // Copy buffer to image
    m_skyboxTexture->CopyBufferToImage(
        stagingBuffer.GetBuffer(),
        width, height
    );

    // Transition to ShaderReadOnly
    m_skyboxTexture->TransitionLayout(
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eComputeShader
    );

    // Create sampler
    VulkanSamplerSpec samplerSpec{
        vk::Filter::eLinear,
        vk::Filter::eLinear,
        vk::SamplerMipmapMode::eLinear,
        vk::SamplerAddressMode::eRepeat,
        vk::SamplerAddressMode::eRepeat,
        vk::SamplerAddressMode::eRepeat
    };
    m_skyboxTexture->CreateSampler(samplerSpec);
    
    stagingBuffer.Destroy();
    
    UpdateComputeDescriptorSet();
}

void BlackHoleRenderer::Resize(int width, int height) {
    if (width == m_width && height == m_height) {
        return;
    }

    m_width = width;
    m_height = height;

    spdlog::info("BlackHoleRenderer::Resize - Resizing to {}x{}", width, height);

    // Recreate compute texture
    if (m_computeTexture) {
        m_computeTexture->Destroy();
    }
    CreateComputeTexture();

    // Recreate bloom textures
    if (m_bloomBrightTexture) {
        m_bloomBrightTexture->Destroy();
    }
    for (int i = 0; i < 2; i++) {
        if (m_bloomBlurTexture[i]) {
            m_bloomBlurTexture[i]->Destroy();
        }
    }
    CreateBloomTextures();
    if (m_ImGuiDescriptorSet) { m_ImGuiDescriptorSet = VK_NULL_HANDLE; }
    UpdateDisplayDescriptorSet();
    UpdateComputeDescriptorSet();

    spdlog::info("BlackHoleRenderer::Resize - Resize complete");
}

void BlackHoleRenderer::Shutdown() {
    if (!m_Device) return;
    auto device = m_Device->GetDevice();

    if (m_ImGuiDescriptorSet) {
        m_ImGuiDescriptorSet = VK_NULL_HANDLE;
    }

    if (m_computePipeline) { m_computePipeline->Destroy(); m_computePipeline.reset(); }
    if (m_displayPipeline) { m_displayPipeline->Destroy(); m_displayPipeline.reset(); }
    
    m_computeShader.reset();
    m_displayShader.reset();

    if (m_ComputeDescriptorPool) {
        device.destroyDescriptorPool(m_ComputeDescriptorPool);
        m_ComputeDescriptorPool = nullptr;
    }

    if (m_ComputeDescriptorSetLayout) {
        device.destroyDescriptorSetLayout(m_ComputeDescriptorSetLayout);
        m_ComputeDescriptorSetLayout = nullptr;
    }

    if (m_DisplayDescriptorSetLayout) {
        device.destroyDescriptorSetLayout(m_DisplayDescriptorSetLayout);
        m_DisplayDescriptorSetLayout = nullptr;
    }

    if (m_CommonUBO) { m_CommonUBO->Destroy(); m_CommonUBO.reset(); }
    if (m_DisplayUBO) { m_DisplayUBO->Destroy(); m_DisplayUBO.reset(); }
    if (m_meshDataSSBO) { m_meshDataSSBO->Destroy(); m_meshDataSSBO.reset(); }
    if (m_triangleSSBO) { m_triangleSSBO->Destroy(); m_triangleSSBO.reset(); }

    m_computeTexture.reset();
    m_skyboxTexture.reset();
    m_blackbodyLUT.reset();
    m_accelerationLUT.reset();
    m_hrDiagramLUT.reset();
}

vk::DescriptorSet BlackHoleRenderer::GetImGuiDescriptorSet() {
    if (!m_computeTexture) return VK_NULL_HANDLE;
    if (m_ImGuiDescriptorSet) return m_ImGuiDescriptorSet;

    m_ImGuiDescriptorSet = ImGui_ImplVulkan_AddTexture(
        m_computeTexture->GetSampler(),
        m_computeTexture->GetImageView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    return m_ImGuiDescriptorSet;
}
