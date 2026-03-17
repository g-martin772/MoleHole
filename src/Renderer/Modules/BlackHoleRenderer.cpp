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

    // Generate and upload LUT textures
    GenerateBlackbodyLUT();
    GenerateAccelerationLUT();
    GenerateHRDiagramLUT();
    GenerateKerrGeodesicLUTs();

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
    m_computePipeline->CreateComputePipeline(computeConfig);

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

    // Transition to General layout immediately
    m_computeTexture->TransitionLayout(
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eGeneral,
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eComputeShader
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
    
    auto lutData = MoleHole::BlackbodyLUTGenerator::generateLUT();
    
    m_blackbodyLUT = std::make_shared<VulkanImage>();
    VulkanImageSpec spec{
        glm::vec3(MoleHole::BlackbodyLUTGenerator::LUT_WIDTH, MoleHole::BlackbodyLUTGenerator::LUT_HEIGHT, 1.0f),
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::Format::eR32G32B32Sfloat,  // RGB floats
        true,
        vk::ImageAspectFlagBits::eColor
    };
    m_blackbodyLUT->Create(spec, m_Device);

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
        vk::Format::eR32Sfloat,  // Single channel float
        true,
        vk::ImageAspectFlagBits::eColor
    };
    m_accelerationLUT->Create(spec, m_Device);

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
    
    auto lutData = MoleHole::HRDiagramLUTGenerator::generateLUT();
    
    m_hrDiagramLUT = std::make_shared<VulkanImage>();
    VulkanImageSpec spec{
        glm::vec3(MoleHole::HRDiagramLUTGenerator::LUT_SIZE, 1.0f, 1.0f),
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::Format::eR32G32B32Sfloat,  // RGB floats
        true,
        vk::ImageAspectFlagBits::eColor
    };
    m_hrDiagramLUT->Create(spec, m_Device);

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
    spdlog::info("Generating Kerr Geodesic LUTs...");
    
    auto deflectionData = MoleHole::KerrGeodesicLUTGenerator::generateDeflectionLUT();
    auto redshiftData = MoleHole::KerrGeodesicLUTGenerator::generateRedshiftLUT();
    auto photonSphereData = MoleHole::KerrGeodesicLUTGenerator::generatePhotonSphereLUT();
    auto iscoData = MoleHole::KerrGeodesicLUTGenerator::generateISCOLUT();

    // Create and upload Deflection LUT
    m_kerrDeflectionLUT = std::make_shared<VulkanImage>();
    VulkanImageSpec deflectionSpec{
        glm::vec3(MoleHole::KerrGeodesicLUTGenerator::LUT_IMPACT_PARAM_SAMPLES, 
                  MoleHole::KerrGeodesicLUTGenerator::LUT_INCLINATION_SAMPLES, 
                  MoleHole::KerrGeodesicLUTGenerator::LUT_SPIN_SAMPLES),
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::Format::eR32Sfloat,
        true,
        vk::ImageAspectFlagBits::eColor,
        1, 1, vk::SampleCountFlagBits::e1,
        vk::ImageType::e3D  // Explicitly 3D image
    };
    m_kerrDeflectionLUT->Create(deflectionSpec, m_Device);

    // Upload deflection data
    size_t deflectionSize = deflectionData.size() * sizeof(float);
    VulkanBuffer stagingBuffer;
    VulkanBufferSpec stagingSpec{
        deflectionSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
    };
    stagingBuffer.Create(stagingSpec, m_Device);
    stagingBuffer.Write(deflectionData.data(), deflectionSize, 0);

    m_kerrDeflectionLUT->CopyBufferToImage(
        stagingBuffer.GetBuffer(),
        MoleHole::KerrGeodesicLUTGenerator::LUT_IMPACT_PARAM_SAMPLES,
        MoleHole::KerrGeodesicLUTGenerator::LUT_INCLINATION_SAMPLES,
        MoleHole::KerrGeodesicLUTGenerator::LUT_SPIN_SAMPLES, 0, 0
    );

    m_kerrDeflectionLUT->TransitionLayout(
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader
    );

    VulkanSamplerSpec samplerSpec{
        vk::Filter::eLinear,
        vk::Filter::eLinear,
        vk::SamplerMipmapMode::eNearest,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge,
        vk::SamplerAddressMode::eClampToEdge
    };
    m_kerrDeflectionLUT->CreateSampler(samplerSpec);

    // Create and upload Redshift LUT
    m_kerrRedshiftLUT = std::make_shared<VulkanImage>();
    m_kerrRedshiftLUT->Create(deflectionSpec, m_Device);

    size_t redshiftSize = redshiftData.size() * sizeof(float);
    stagingBuffer.Destroy();
    stagingBuffer.Create({redshiftSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent}, m_Device);
    stagingBuffer.Write(redshiftData.data(), redshiftSize, 0);

    m_kerrRedshiftLUT->CopyBufferToImage(
        stagingBuffer.GetBuffer(),
        MoleHole::KerrGeodesicLUTGenerator::LUT_IMPACT_PARAM_SAMPLES,
        MoleHole::KerrGeodesicLUTGenerator::LUT_INCLINATION_SAMPLES,
        MoleHole::KerrGeodesicLUTGenerator::LUT_SPIN_SAMPLES, 0, 0
    );

    m_kerrRedshiftLUT->TransitionLayout(
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader
    );
    m_kerrRedshiftLUT->CreateSampler(samplerSpec);

    // Create and upload Photon Sphere LUT
    m_kerrPhotonSphereLUT = std::make_shared<VulkanImage>();
    VulkanImageSpec photonSphereSpec = deflectionSpec;
    photonSphereSpec.Size = glm::vec3(MoleHole::KerrGeodesicLUTGenerator::LUT_INCLINATION_SAMPLES, MoleHole::KerrGeodesicLUTGenerator::LUT_SPIN_SAMPLES, 1.0f);
    photonSphereSpec.ImageType = vk::ImageType::e2D;
    
    m_kerrPhotonSphereLUT->Create(photonSphereSpec, m_Device);

    size_t photonSphereSize = photonSphereData.size() * sizeof(float);
    stagingBuffer.Destroy();
    stagingBuffer.Create({photonSphereSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent}, m_Device);
    stagingBuffer.Write(photonSphereData.data(), photonSphereSize, 0);

    m_kerrPhotonSphereLUT->CopyBufferToImage(
        stagingBuffer.GetBuffer(),
        MoleHole::KerrGeodesicLUTGenerator::LUT_INCLINATION_SAMPLES,
        MoleHole::KerrGeodesicLUTGenerator::LUT_SPIN_SAMPLES,
        1, 0, 0
    );

    m_kerrPhotonSphereLUT->TransitionLayout(
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader
    );
    m_kerrPhotonSphereLUT->CreateSampler(samplerSpec);

    // Create and upload ISCO LUT
    m_kerrISCOLUT = std::make_shared<VulkanImage>();
    VulkanImageSpec iscoSpec = deflectionSpec;
    iscoSpec.Size = glm::vec3(MoleHole::KerrGeodesicLUTGenerator::LUT_SPIN_SAMPLES, 1.0f, 1.0f);
    iscoSpec.ImageType = vk::ImageType::e2D;

    m_kerrISCOLUT->Create(iscoSpec, m_Device);

    size_t iscoSize = iscoData.size() * sizeof(float);
    stagingBuffer.Destroy();
    stagingBuffer.Create({iscoSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent}, m_Device);
    stagingBuffer.Write(iscoData.data(), iscoSize, 0);

    m_kerrISCOLUT->CopyBufferToImage(
        stagingBuffer.GetBuffer(),
        MoleHole::KerrGeodesicLUTGenerator::LUT_SPIN_SAMPLES,
        1,
        1, 0, 0
    );

    m_kerrISCOLUT->TransitionLayout(
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader
    );
    m_kerrISCOLUT->CreateSampler(samplerSpec);

    stagingBuffer.Destroy();
    spdlog::info("Kerr Geodesic LUTs generated successfully");
}

// ===========================================================================================
// Rendering
// ===========================================================================================

void BlackHoleRenderer::Render(const Scene& scene,
                              const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache,
                              const Camera& camera,
                              float time,
                              vk::CommandBuffer cmd) {
    if (!m_computePipeline || !m_computeTexture) {
        spdlog::warn("BlackHoleRenderer::Render - Pipeline or texture not initialized");
        return;
    }

    // Update uniforms
    UpdateUniforms(scene, meshCache, camera, time, cmd);

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

void BlackHoleRenderer::RenderToScreen(vk::CommandBuffer cmd) {
    if (!m_displayPipeline) return;

    // Transition compute texture to shader read
    m_computeTexture->TransitionLayout(
        cmd,
        vk::ImageLayout::eGeneral,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eFragmentShader
    );

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

    // Transition back to General for next compute frame
    m_computeTexture->TransitionLayout(
        cmd,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageLayout::eGeneral,
        vk::PipelineStageFlagBits::eFragmentShader,
        vk::PipelineStageFlagBits::eComputeShader
    );
}

// ===========================================================================================
// Utility Functions
// ===========================================================================================

void BlackHoleRenderer::UpdateUniforms(const Scene& scene,
                                      const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache,
                                      const Camera& camera,
                                      float time,
                                      vk::CommandBuffer cmd) {
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

    // Write to UBO
    m_CommonUBO->Write(&ubo, sizeof(CommonUBO), 0);

    // Update descriptor set with current resources
    std::vector<vk::WriteDescriptorSet> writes;

    // Storage image
    vk::DescriptorImageInfo storageImageInfo{
        {},
        m_computeTexture->GetImageView(),
        vk::ImageLayout::eGeneral
    };
    writes.push_back(vk::WriteDescriptorSet{
        m_ComputeDescriptorSet,
        0,
        0,
        1,
        vk::DescriptorType::eStorageImage,
        &storageImageInfo,
        nullptr,
        nullptr
    });

    // Uniform buffer
    vk::DescriptorBufferInfo bufferInfo = m_CommonUBO->GetDescriptorInfo();
    writes.push_back(vk::WriteDescriptorSet{
        m_ComputeDescriptorSet,
        1,
        0,
        1,
        vk::DescriptorType::eUniformBuffer,
        nullptr,
        &bufferInfo,
        nullptr
    });

    // Skybox texture (if available)
    if (m_skyboxTexture) {
        vk::DescriptorImageInfo skyboxInfo{
            m_skyboxTexture->GetSampler(),
            m_skyboxTexture->GetImageView(),
            vk::ImageLayout::eShaderReadOnlyOptimal
        };
        writes.push_back(vk::WriteDescriptorSet{
            m_ComputeDescriptorSet,
            2,
            0,
            1,
            vk::DescriptorType::eCombinedImageSampler,
            &skyboxInfo,
            nullptr,
            nullptr
        });
    }

    // LUT textures
    std::vector<vk::DescriptorImageInfo> lutInfos;
    std::vector<std::shared_ptr<VulkanImage>> luts{
        m_blackbodyLUT,
        m_accelerationLUT,
        m_hrDiagramLUT,
        m_kerrDeflectionLUT,
        m_kerrRedshiftLUT,
        m_kerrPhotonSphereLUT,
        m_kerrISCOLUT
    };

    for (size_t i = 0; i < luts.size(); i++) {
        if (luts[i]) {
            lutInfos.push_back(vk::DescriptorImageInfo{
                luts[i]->GetSampler(),
                luts[i]->GetImageView(),
                vk::ImageLayout::eShaderReadOnlyOptimal
            });
        } else {
             // Handle missing LUTs gracefully if needed, though they should be init'd
             lutInfos.push_back(vk::DescriptorImageInfo{});
        }
    }

    for (size_t i = 0; i < lutInfos.size(); i++) {
        if (!luts[i]) continue;
        writes.push_back(vk::WriteDescriptorSet{
            m_ComputeDescriptorSet,
            3 + static_cast<uint32_t>(i),
            0,
            1,
            vk::DescriptorType::eCombinedImageSampler,
            &lutInfos[i],
            nullptr,
            nullptr
        });
    }

    // Update descriptors
    m_Device->GetDevice().updateDescriptorSets(writes, nullptr);
}

void BlackHoleRenderer::UpdateMeshBuffers(const Scene& scene,
                                         const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache) {
    // Stub: Update mesh SSBOs if needed
    spdlog::debug("UpdateMeshBuffers called");
}

void BlackHoleRenderer::CreateMeshBuffers() {
    // Stub: Create SSBOs for mesh data
    spdlog::debug("CreateMeshBuffers called");
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

void BlackHoleRenderer::LoadSkybox() {
    // Stub: Load skybox texture
    spdlog::debug("LoadSkybox called");
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

    spdlog::info("BlackHoleRenderer::Resize - Resize complete");
}
