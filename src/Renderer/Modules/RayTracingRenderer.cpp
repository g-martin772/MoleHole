#include "RayTracingRenderer.h"
#include "Simulation/Scene.h"
#include "Renderer/Interface/Camera.h"
#include "Renderer/Models/GLTFMesh.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "imgui_impl_vulkan.h"
#include "Application/Application.h"
#include "Application/Parameters.h"

#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <imgui.h>

RayTracingRenderer::RayTracingRenderer() {
}

RayTracingRenderer::~RayTracingRenderer() {
    Shutdown();
}

void RayTracingRenderer::Init(VulkanDevice* device, int width, int height) {
    m_device = device;
    m_width = width;
    m_height = height;

    CreateOutputImage();

    // Create Camera UBO
    m_cameraUBO = std::make_unique<VulkanBuffer>();
    VulkanBufferSpec uboSpec{};
    uboSpec.Size = sizeof(CameraProperties);
    uboSpec.Usage = vk::BufferUsageFlagBits::eUniformBuffer;
    uboSpec.MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    m_cameraUBO->Create(uboSpec, m_device);

    // Create Descriptor Set Layout
    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        // Binding 0: Top Level Acceleration Structure
        {0, vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR},
        // Binding 1: Output Image
        {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR},
        // Binding 2: Camera UBO
        {2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eRaygenKHR}
    };

    vk::DescriptorSetLayoutCreateInfo layoutInfo({}, bindings);
    try {
        m_descriptorSetLayout = m_device->GetDevice().createDescriptorSetLayout(layoutInfo);
    } catch (const vk::SystemError& err) {
        spdlog::error("Failed to create RayTracing DescriptorSetLayout: {}", err.what());
        return;
    }

    // Create Pipeline
    m_pipeline = std::make_unique<VulkanRayTracingPipeline>();
    m_pipeline->Init(m_device);
    m_pipeline->CreatePipeline(
        "shaders/rt/raygen.rgen",
        "shaders/rt/miss.rmiss",
        "shaders/rt/closesthit.rchit",
        {m_descriptorSetLayout}
    );

    // Create Descriptor Pool
    // Increase pool size to allow multiple frames in flight (deferred destruction)
    std::vector<vk::DescriptorPoolSize> poolSizes = {
        {vk::DescriptorType::eAccelerationStructureKHR, 100},
        {vk::DescriptorType::eStorageImage, 100},
        {vk::DescriptorType::eUniformBuffer, 100}
    };

    vk::DescriptorPoolCreateInfo poolInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 100, poolSizes);
    try {
        m_descriptorPool = m_device->GetDevice().createDescriptorPool(poolInfo);
    } catch (const vk::SystemError& err) {
        spdlog::error("Failed to create RayTracing DescriptorPool: {}", err.what());
        return;
    }

    CreateDescriptorSets();
    
    if (m_descriptorSet) {
        m_initialized = true;
    }
}

void RayTracingRenderer::CreateOutputImage() {
    m_outputImage = std::make_shared<VulkanImage>();
    VulkanImageSpec spec;
    spec.Size = glm::vec3(m_width, m_height, 1);
    spec.Format = vk::Format::eR32G32B32A32Sfloat;
    spec.Usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
    spec.MemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
    m_outputImage->Create(spec, m_device);

    // Transition to ShaderReadOnlyOptimal immediately to ensure it's valid for ImGui
    // even if Render() hasn't run yet (e.g. first frame or zero viewport size).
    // This uses a synchronous single-time command buffer.
    m_outputImage->TransitionLayout(
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eFragmentShader
    );
}

void RayTracingRenderer::CreateDescriptorSets() {
    if (!m_descriptorPool || !m_descriptorSetLayout) {
        spdlog::error("Skipping CreateDescriptorSets: Descriptor Pool or Layout is null");
        return;
    }

    vk::DescriptorSetAllocateInfo allocInfo(m_descriptorPool, 1, &m_descriptorSetLayout);
    try {
        auto sets = m_device->GetDevice().allocateDescriptorSets(allocInfo);
        m_descriptorSet = sets[0];
    } catch (const vk::SystemError& err) {
        spdlog::error("Failed to allocate descriptor sets: {}", err.what());
        return;
    }

    // Update Image and UBO descriptors (TLAS updated later)
    vk::DescriptorImageInfo imageInfo(
        {},
        m_outputImage->GetImageView(),
        vk::ImageLayout::eGeneral
    );

    vk::DescriptorBufferInfo bufferInfo(
        m_cameraUBO->GetBuffer(),
        0,
        sizeof(CameraProperties)
    );

    std::vector<vk::WriteDescriptorSet> descriptorWrites = {
        {m_descriptorSet, 1, 0, 1, vk::DescriptorType::eStorageImage, &imageInfo},
        {m_descriptorSet, 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufferInfo}
    };

    m_device->GetDevice().updateDescriptorSets(descriptorWrites, {});
}

void RayTracingRenderer::Resize(int width, int height) {
    if (m_width == width && m_height == height) return;
    
    m_width = width;
    m_height = height;

    // Queue old resources for destruction
    if (m_descriptorSet) {
        PendingResource pending;
        pending.descriptorSet = m_descriptorSet;
        pending.isImGuiDescriptor = false;
        pending.frameIndex = m_frameCount;
        m_pendingResources.push_back(std::move(pending));
        m_descriptorSet = VK_NULL_HANDLE;
    }

    if (m_imGuiDescriptorSet) {
        PendingResource pending;
        pending.image = m_outputImage; // Keep old image alive
        pending.descriptorSet = m_imGuiDescriptorSet;
        pending.isImGuiDescriptor = true;
        pending.frameIndex = m_frameCount;
        m_pendingResources.push_back(std::move(pending));
        m_imGuiDescriptorSet = VK_NULL_HANDLE;
    }

    CreateOutputImage();
    
    // Ensure UBO exists for descriptor creation
    if (!m_cameraUBO) {
        m_cameraUBO = std::make_unique<VulkanBuffer>();
        VulkanBufferSpec spec{};
        spec.Size = sizeof(CameraProperties);
        spec.Usage = vk::BufferUsageFlagBits::eUniformBuffer;
        spec.MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        m_cameraUBO->Create(spec, m_device);
    }
    
    CreateDescriptorSets();
}

void RayTracingRenderer::ProcessPendingResources() {
    uint64_t currentFrame = m_frameCount;
    // Keep resources for at least 3 frames (safe for triple buffering)
    const uint64_t safeFrameAge = 3; 

    auto it = m_pendingResources.begin();
    while (it != m_pendingResources.end()) {
        if (currentFrame > it->frameIndex + safeFrameAge) {
            if (it->descriptorSet) {
                if (it->isImGuiDescriptor) {
                    ImGui_ImplVulkan_RemoveTexture(it->descriptorSet);
                } else {
                    (void)m_device->GetDevice().freeDescriptorSets(m_descriptorPool, 1, &it->descriptorSet);
                }
            }
            // Smart pointers will automatically release resources
            it = m_pendingResources.erase(it);
        } else {
            ++it;
        }
    }
}

#include <imgui.h>

void RayTracingRenderer::RenderUI() {
    if (ImGui::CollapsingHeader("Ray Tracing Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        int mode = Application::Params().Get(Params::VideoRayTracingMode, 0);
        const char* modes[] = { "Euclidean (Straight)", "Kerr (Bent)" };
        if (ImGui::Combo("Mode", &mode, modes, IM_ARRAYSIZE(modes))) {
            Application::Params().Set(Params::VideoRayTracingMode, mode);
        }
        
        if (mode == 1) {
            float stepSize = Application::Params().Get(Params::VideoRayMarchStepSize, 0.1f);
            if (ImGui::DragFloat("Step Size", &stepSize, 0.01f, 0.01f, 10.0f)) {
                Application::Params().Set(Params::VideoRayMarchStepSize, stepSize);
            }
            ImGui::TextWrapped("Note: Kerr metric not fully implemented yet. Using straight rays.");
        }
    }
}

void RayTracingRenderer::UpdateUniformBuffer(const Camera& camera, const Scene& scene) {
    if (!m_cameraUBO) return;

    CameraProperties ubo{};
    ubo.viewInverse = glm::inverse(camera.GetViewMatrix());
    ubo.projInverse = glm::inverse(camera.GetProjectionMatrix());
    ubo.mode = Application::Params().Get(Params::VideoRayTracingMode, 0);
    ubo.maxSteps = Application::Params().Get(Params::VideoRayMarchSteps, 100);
    ubo.stepSize = Application::Params().Get(Params::VideoRayMarchStepSize, 0.1f);

    // Populate Black Holes
    ubo.numBlackHoles = 0;
    ubo.numSpheres = 0;

    ParameterHandle posHandle("Entity.Position");
    ParameterHandle rotHandle("Entity.Rotation");
    ParameterHandle massHandle("Physics.Mass");
    ParameterHandle spinHandle("BlackHole.Spin");
    ParameterHandle sphereRadiusHandle("Sphere.Radius");
    ParameterHandle sphereColorHandle("Sphere.Color");
    
    // Scale factor: 1477.0f meters per geometric unit (M_sun in geometrical units)
    const float GEOMETRIC_UNIT_METERS = 1477.0f;
    const float SOLAR_MASS_KG = 1.98847e30f;

    for (const auto& obj : scene.objects) {
        if (obj.HasClass("BlackHole") && ubo.numBlackHoles < 8) {
            glm::vec3 pos = obj.GetParameter<glm::vec3>(posHandle).value_or(glm::vec3(0.0f));
            glm::quat rot = obj.GetParameter<glm::quat>(rotHandle).value_or(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            float massKg = obj.GetParameter<float>(massHandle).value_or(SOLAR_MASS_KG);
            float spinMag = obj.GetParameter<float>(spinHandle).value_or(0.0f);
            
            float massSM = massKg / SOLAR_MASS_KG;
            
            // Spin Axis: Assume Y-axis is the local up/spin axis
            glm::vec3 spinAxis = rot * glm::vec3(0.0f, 1.0f, 0.0f);
            
            // Position in Geometric Units
            ubo.blackHoles[ubo.numBlackHoles] = glm::vec4(pos / GEOMETRIC_UNIT_METERS, massSM);
            ubo.blackHoleSpins[ubo.numBlackHoles] = glm::vec4(spinAxis, spinMag);
            
            ubo.numBlackHoles++;
        }
        else if (obj.HasClass("Sphere") && ubo.numSpheres < 8) {
            glm::vec3 pos = obj.GetParameter<glm::vec3>(posHandle).value_or(glm::vec3(0.0f));
            float radius = obj.GetParameter<float>(sphereRadiusHandle).value_or(1000.0f); // Default 1km radius
            glm::vec3 color = obj.GetParameter<glm::vec3>(sphereColorHandle).value_or(glm::vec3(1.0f));

            // Convert to Geometric Units
            ubo.spheres[ubo.numSpheres] = glm::vec4(pos / GEOMETRIC_UNIT_METERS, radius / GEOMETRIC_UNIT_METERS);
            ubo.sphereColors[ubo.numSpheres] = glm::vec4(color, 1.0f);
            ubo.numSpheres++;
        }
    }

    m_cameraUBO->Write(&ubo, sizeof(CameraProperties));
}

void RayTracingRenderer::CreateAccelerationStructures(const Scene& scene, const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache, vk::CommandBuffer cmd) {
    // 1. Build BLAS for all meshes
    std::vector<vk::AccelerationStructureInstanceKHR> instances;

    // Helper to add instance
    auto addInstance = [&](VulkanAccelerationStructure* blas, const glm::mat4& transform, uint32_t customIndex, uint32_t sbtOffset) {
        vk::AccelerationStructureInstanceKHR instance{};
        instance.transform = ToVkTransform(transform);
        instance.instanceCustomIndex = customIndex;
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = sbtOffset;
        instance.flags = static_cast<VkGeometryInstanceFlagsKHR>(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
        instance.accelerationStructureReference = blas->GetDeviceAddress();
        instances.push_back(instance);
    };

    // Process Scene Objects
    ParameterHandle meshPathHandle("Mesh.FilePath");
    ParameterHandle posHandle("Entity.Position");
    ParameterHandle rotHandle("Entity.Rotation");
    ParameterHandle scaleHandle("Entity.Scale");

    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const auto& obj = scene.objects[i];
        if (!obj.HasClass("Mesh")) continue;

        auto pathVal = obj.GetParameter<std::string>(meshPathHandle);
        if (!pathVal) continue;
        std::string path = *pathVal;
        
        // Find mesh in cache
        auto it = meshCache.find(path);
        if (it == meshCache.end() || !it->second) continue;
        
        GLTFMesh* mesh = it->second.get();

        // Check cache for BLAS
        if (m_blasCache.find(mesh) == m_blasCache.end()) {
            // Build BLAS
            std::vector<BLASInput> inputs;
            const auto& primitives = mesh->GetPrimitives();
            
            for (const auto& prim : primitives) {
                if (!prim.m_VertexBuffer || !prim.m_IndexBuffer) continue;
                
                BLASInput input{};
                input.vertexBuffer = prim.m_VertexBuffer.get();
                input.indexBuffer = prim.m_IndexBuffer.get();
                // GLTFMesh uses packed buffer: pos(3), norm(3), uv(2) -> 8 floats
                input.vertexStride = 8 * sizeof(float); 
                input.indexCount = prim.m_indexCount;
                input.vertexCount = static_cast<uint32_t>(prim.m_VertexBuffer->GetSize() / input.vertexStride);
                inputs.push_back(input);
            }

            if (inputs.empty()) continue;

            auto blas = std::make_unique<VulkanAccelerationStructure>();
            blas->BuildBLAS(m_device, cmd, inputs);
            m_blasCache[mesh] = std::move(blas);
        }

        // Add Instance
        glm::vec3 position = obj.GetParameter<glm::vec3>(posHandle).value_or(glm::vec3(0.0f));
        glm::quat rotation = obj.GetParameter<glm::quat>(rotHandle).value_or(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        glm::vec3 scale = obj.GetParameter<glm::vec3>(scaleHandle).value_or(glm::vec3(1.0f));

        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, position);
        transform *= glm::mat4_cast(rotation);
        transform = glm::scale(transform, scale);

        addInstance(m_blasCache[mesh].get(), transform, static_cast<uint32_t>(i), 0);
    }
    
    // Process Spheres (Placeholder - use a unit sphere mesh if available, or skip for now)
    // TODO: Add spheres

    if (instances.empty()) return;

    vk::DeviceSize bufferSize = instances.size() * sizeof(vk::AccelerationStructureInstanceKHR);

    // Always reallocate instance buffer to avoid race condition with previous frame
    if (m_instanceBuffer) {
        PendingResource pending;
        pending.buffer = std::move(m_instanceBuffer);
        pending.frameIndex = m_frameCount;
        m_pendingResources.push_back(std::move(pending));
    }

    VulkanBufferSpec spec{};
    spec.Size = bufferSize;
    spec.Usage = vk::BufferUsageFlagBits::eShaderDeviceAddress | 
                 vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                 vk::BufferUsageFlagBits::eTransferDst;
    spec.MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    
    m_instanceBuffer = std::make_unique<VulkanBuffer>();
    m_instanceBuffer->Create(spec, m_device);

    m_instanceBuffer->Write(instances.data(), bufferSize);

    // Always rebuild TLAS for now (dynamic scene)
    // We create a new TLAS and defer the old one to avoid destroying it while in use by previous frame
    auto newTLAS = std::make_unique<VulkanAccelerationStructure>();
    newTLAS->BuildTLAS(m_device, cmd, m_instanceBuffer.get(), static_cast<uint32_t>(instances.size()));
    
    if (m_tlas) {
        PendingResource pending;
        pending.accelerationStructure = std::move(m_tlas);
        pending.frameIndex = m_frameCount;
        m_pendingResources.push_back(std::move(pending));
    }
    m_tlas = std::move(newTLAS);
    
    // Update Descriptor Set with new TLAS
    vk::WriteDescriptorSetAccelerationStructureKHR asInfo{};
    asInfo.accelerationStructureCount = 1;
    vk::AccelerationStructureKHR tlasHandle = m_tlas->GetHandle();
    asInfo.pAccelerationStructures = &tlasHandle;

    vk::WriteDescriptorSet descriptorWrite{};
    descriptorWrite.dstSet = m_descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pNext = &asInfo;

    m_device->GetDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
}

void RayTracingRenderer::Render(const Scene& scene, const Camera& camera, const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache, vk::CommandBuffer cmd) {
    if (!m_outputImage) return;

    m_frameCount++;
    ProcessPendingResources();

    // Transition image to GENERAL for clearing/writing
    // Use eUndefined as old layout to discard contents
    m_outputImage->TransitionLayout(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, 
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eRayTracingShaderKHR);

    // Clear image to black (or background color)
    vk::ClearColorValue clearColor(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    cmd.clearColorImage(m_outputImage->GetImage(), vk::ImageLayout::eGeneral, &clearColor, 1, &range);

    // Check if we can proceed with ray tracing
    if (!m_initialized) {
        // Just transition to ReadOnly so ImGui can display the black image
        m_outputImage->TransitionLayout(cmd, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal, 
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader);
        return;
    }

    // Ensure descriptor set exists
    if (!m_descriptorSet) {
        CreateDescriptorSets();
    }
    
    // If we can't ray trace (no descriptors or empty scene), transition to ReadOnly and return.
    bool canTrace = (m_descriptorSet && m_tlas && m_tlas->GetHandle());

    if (canTrace) {
        UpdateUniformBuffer(camera, scene);
        CreateAccelerationStructures(scene, meshCache, cmd);
        
        // Re-check TLAS because CreateAS might have failed or returned empty
        if (m_tlas && m_tlas->GetHandle()) {
            m_pipeline->Bind(cmd);
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eRayTracingKHR,
                m_pipeline->GetLayout(),
                0, 1, &m_descriptorSet, 0, nullptr
            );
            cmd.traceRaysKHR(
                &m_pipeline->GetRayGenRegion(),
                &m_pipeline->GetMissRegion(),
                &m_pipeline->GetHitRegion(),
                &m_pipeline->GetCallRegion(),
                m_width, m_height, 1,
                m_device->GetDispatcher()
            );
        }
    }

    // Transition image to SHADER_READ_ONLY_OPTIMAL for display
    m_outputImage->TransitionLayout(cmd, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal, 
        vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eRayTracingShaderKHR, 
        vk::PipelineStageFlagBits::eFragmentShader);
}


vk::TransformMatrixKHR RayTracingRenderer::ToVkTransform(const glm::mat4& matrix) {
    glm::mat4 transposed = glm::transpose(matrix);
    vk::TransformMatrixKHR out;
    memcpy(&out, glm::value_ptr(transposed), sizeof(vk::TransformMatrixKHR));
    return out;
}

void RayTracingRenderer::Shutdown() {
    // Clean up pending resources first
    for (auto& pending : m_pendingResources) {
        if (pending.descriptorSet && pending.isImGuiDescriptor) {
            ImGui_ImplVulkan_RemoveTexture(pending.descriptorSet);
        }
        // Other resources (buffers, AS) are managed by smart pointers and will be released
    }
    m_pendingResources.clear();

    if (m_device) {
        auto device = m_device->GetDevice();
        if (m_descriptorPool) {
            device.destroyDescriptorPool(m_descriptorPool);
            m_descriptorPool = VK_NULL_HANDLE;
        }
        if (m_descriptorSetLayout) {
            device.destroyDescriptorSetLayout(m_descriptorSetLayout);
            m_descriptorSetLayout = VK_NULL_HANDLE;
        }
        if (m_imGuiDescriptorSet) {
             ImGui_ImplVulkan_RemoveTexture(m_imGuiDescriptorSet);
             m_imGuiDescriptorSet = VK_NULL_HANDLE;
        }
    }
    m_pipeline.reset();
    m_tlas.reset();
    m_cameraUBO.reset();
    m_outputImage.reset();
}

vk::DescriptorSet RayTracingRenderer::GetImGuiDescriptorSet() {
    if (!m_outputImage) return VK_NULL_HANDLE;
    if (m_imGuiDescriptorSet) return m_imGuiDescriptorSet;

    // We need a sampler for ImGui
    if (!m_outputImage->HasSampler()) {
        VulkanSamplerSpec samplerSpec{
            vk::Filter::eLinear,
            vk::Filter::eLinear,
            vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eRepeat,
            vk::SamplerAddressMode::eRepeat,
            vk::SamplerAddressMode::eRepeat
        };
        m_outputImage->CreateSampler(samplerSpec);
    }

    m_imGuiDescriptorSet = ImGui_ImplVulkan_AddTexture(
        m_outputImage->GetSampler(),
        m_outputImage->GetImageView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
    return m_imGuiDescriptorSet;
}
