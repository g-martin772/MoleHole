#include "Renderer.h"
#include "Modules/PhysicsDebugRenderer.h"
#include <GLFW/glfw3.h>
#include "ImGuizmo.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "RenderCommand.h"
#include "Interface/Camera.h"
#include "Interface/Input.h"
#include "Application/Application.h"
#include "Application/Parameters.h"
#include "Modules/GravityGridRenderer.h"
#include "Platform/Vulkan/VulkanBuffer.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

struct SceneData {
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec4 cameraPos;
    glm::vec4 lightDir;
};

struct PushConstants {
    glm::mat4 model;
    glm::vec4 color;
};

void Renderer::Init() {
    Init(false);
}

void Renderer::Init(bool headless) {
    WindowProperties props;
    props.width = 1280;
    props.height = 720;
    props.title = "MoleHole Window";
    props.visible = !headless;
    props.vsync = true;
    // ClientAPI is handled by Window implementation (GLFW_NO_API)

    m_window = Window::Create(props);
    if (!m_window) {
        spdlog::error("Failed to create window");
        exit(-1);
    }

    input = std::make_unique<Input>(m_window->GetNativeWindow());

    // Initialize Vulkan
    m_VulkanApi = std::make_unique<VulkanApi>();
    m_VulkanApi->Init();

    if (!m_VulkanApi->GetDevice().GetDevice()) {
        spdlog::error("VulkanApi failed to initialize (Device is null). Aborting Renderer initialization.");
        return;
    }

    blackHoleRenderer = std::make_unique<BlackHoleRenderer>();
    blackHoleRenderer->Init(&m_VulkanApi->GetDevice(), &m_VulkanApi->GetMainRenderPass(), props.width, props.height);

    m_GravityGridRenderer = std::make_unique<GravityGridRenderer>();
    m_GravityGridRenderer->Init(&m_VulkanApi->GetDevice(), &m_VulkanApi->GetMainRenderPass());

    m_ObjectPathsRenderer = std::make_unique<ObjectPathsRenderer>();
    m_ObjectPathsRenderer->Init(&m_VulkanApi->GetDevice(), &m_VulkanApi->GetMainRenderPass());

    m_physicsDebugRenderer = std::make_unique<PhysicsDebugRenderer>();
    m_physicsDebugRenderer->Init(&m_VulkanApi->GetDevice(), &m_VulkanApi->GetMainRenderPass());

    m_rayTracingRenderer = std::make_unique<RayTracingRenderer>();
    m_rayTracingRenderer->Init(&m_VulkanApi->GetDevice(), props.width, props.height);

    // Create Descriptor Pool
    vk::DescriptorPoolSize poolSizes[] = {
        { vk::DescriptorType::eUniformBuffer, 10 },
        { vk::DescriptorType::eCombinedImageSampler, 10 }
    };
    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 10;
    try {
        m_DescriptorPool = m_VulkanApi->GetDevice().GetDevice().createDescriptorPool(poolInfo);
    } catch (const vk::SystemError& err) {
        spdlog::error("Failed to create Renderer DescriptorPool: {}", err.what());
        return;
    }

    // Create Scene UBO
    VulkanBufferSpec uboSpec{};
    uboSpec.Size = sizeof(SceneData);
    uboSpec.Usage = vk::BufferUsageFlagBits::eUniformBuffer;
    uboSpec.MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    
    m_SceneUBO = std::make_unique<VulkanBuffer>();
    m_SceneUBO->Create(uboSpec, &m_VulkanApi->GetDevice());

    // Load Shaders
    m_MeshShader = std::make_unique<VulkanShader>(&m_VulkanApi->GetDevice(), std::initializer_list<std::pair<ShaderStage, std::string>>{
        { ShaderStage::Vertex, "shaders/mesh.vert" },
        { ShaderStage::Fragment, "shaders/mesh.frag" }
    });

    // Create Pipeline
    GraphicsPipelineConfig config;
    config.device = &m_VulkanApi->GetDevice();
    config.shader = m_MeshShader.get();
    config.renderPass = &m_VulkanApi->GetMainRenderPass();
    
    // Setup vertex input
    config.vertexBindings = {
        { 0, 8 * sizeof(float), vk::VertexInputRate::eVertex }
    };
    config.vertexAttributes = {
        { 0, 0, vk::Format::eR32G32B32Sfloat, 0 }, // Pos
        { 1, 0, vk::Format::eR32G32B32Sfloat, 3 * sizeof(float) }, // Normal
        { 2, 0, vk::Format::eR32G32Sfloat, 6 * sizeof(float) } // TexCoord (vec2)
    };

    // Setup descriptor set layout for Scene Data (Set 0)
    vk::DescriptorSetLayoutBinding sceneBinding{};
    sceneBinding.binding = 0;
    sceneBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    sceneBinding.descriptorCount = 1;
    sceneBinding.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    
    vk::DescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &sceneBinding;
    
    m_SceneLayout = m_VulkanApi->GetDevice().GetDevice().createDescriptorSetLayout(layoutInfo);
    config.descriptorSetLayouts.push_back(m_SceneLayout);

    // Create Texture Layout (Set 1)
    vk::DescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorCount = 1;
    samplerBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    samplerBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
    
    vk::DescriptorSetLayoutCreateInfo textureLayoutInfo{};
    textureLayoutInfo.bindingCount = 1;
    textureLayoutInfo.pBindings = &samplerBinding;
    
    m_TextureLayout = m_VulkanApi->GetDevice().GetDevice().createDescriptorSetLayout(textureLayoutInfo);
    config.descriptorSetLayouts.push_back(m_TextureLayout);

    config.pushConstantRanges = {
        { vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(PushConstants) }
    };

    m_MeshPipeline = std::make_unique<VulkanPipeline>();
    if (!m_MeshPipeline->CreateGraphicsPipeline(config)) {
        spdlog::error("Failed to create mesh pipeline");
    }

    // Camera setup
    int width, height;
    m_window->GetFramebufferSize(width, height);
    camera = std::make_unique<Camera>(Application::Params().Get(Params::RenderingFOV, 45.0f), (float) width / (float) height, 0.01f, 10000.0f);
    camera->SetPosition(Application::Params().Get(Params::CameraPosition, glm::vec3(0.0f, 0.0f, 10.0f)));
    camera->SetYawPitch(Application::Params().Get(Params::CameraYaw, -90.0f), Application::Params().Get(Params::CameraPitch, 0.0f));

    // Allocate Descriptor Set
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_SceneLayout;
    m_SceneDescriptorSet = m_VulkanApi->GetDevice().GetDevice().allocateDescriptorSets(allocInfo)[0];

    // Update Descriptor Set
    vk::DescriptorBufferInfo bufferInfo = m_SceneUBO->GetDescriptorInfo();
    vk::WriteDescriptorSet descriptorWrite{};
    descriptorWrite.dstSet = m_SceneDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;
    m_VulkanApi->GetDevice().GetDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
}

void Renderer::Shutdown() {
    if (m_VulkanApi) {
        m_VulkanApi->GetDevice().GetDevice().waitIdle();
        
        if (blackHoleRenderer) blackHoleRenderer->Shutdown();
        blackHoleRenderer.reset();
        if (m_GravityGridRenderer) m_GravityGridRenderer->Shutdown();
        m_GravityGridRenderer.reset();
        if (m_rayTracingRenderer) m_rayTracingRenderer->Shutdown();
        m_rayTracingRenderer.reset();
        m_ObjectPathsRenderer.reset();
        m_physicsDebugRenderer->Shutdown();
        m_physicsDebugRenderer.reset();

        if (m_DescriptorPool) m_VulkanApi->GetDevice().GetDevice().destroyDescriptorPool(m_DescriptorPool);
        if (m_TextureLayout) m_VulkanApi->GetDevice().GetDevice().destroyDescriptorSetLayout(m_TextureLayout);
        if (m_SceneLayout) m_VulkanApi->GetDevice().GetDevice().destroyDescriptorSetLayout(m_SceneLayout);
        
        if (m_MeshPipeline) m_MeshPipeline->Destroy();
        m_MeshShader.reset();
        if (m_SceneUBO) m_SceneUBO->Destroy();
        m_SceneUBO.reset();
        
        m_VulkanApi->Shutdown();
    }
    if (m_window) {
        delete m_window;
        m_window = nullptr;
    }
}

void Renderer::BeginFrame() {
    m_window->PollEvents();
    if (m_VulkanApi) m_VulkanApi->BeginFrame();
    // ImGuizmo::BeginFrame(); // TODO: Re-enable when ImGuizmo is fixed
}

void Renderer::EndFrame(bool clearScreen) {
    if (m_VulkanApi) {
        m_VulkanApi->EndFrame();
        // Wait for device idle to prevent descriptor update race conditions
        // This is a temporary fix; proper double buffering should be implemented
        m_VulkanApi->GetDevice().GetDevice().waitIdle();
    }
}

void Renderer::RenderScene(Scene *scene) {
    if (scene && camera) scene->camera = camera.get();
    
    // Ensure all meshes in the scene are loaded
    if (scene) {
        // Skybox reloading
        if (scene->reloadSkybox) {
             std::string bgName = Application::Params().Get<std::string>(Params::AppBackgroundImage, "space.hdr");
             std::string bgPath = "../assets/backgrounds/" + bgName;
             
             if (blackHoleRenderer) {
                 blackHoleRenderer->LoadSkybox(bgPath);
             }
             scene->reloadSkybox = false;
        }

        ParameterHandle meshHandle("Mesh");
        for (const auto& obj : scene->objects) {
            if (obj.HasClass("Mesh")) {
                if (auto path = obj.GetParameter<std::string>(meshHandle)) {
                    if (!path->empty()) {
                        GetOrLoadMesh(*path);
                    }
                }
            }
        }
    }
    
    // ImGui Viewport logic
    // int width, height;
    // m_window->GetFramebufferSize(width, height);
    // SetViewportBounds(0, 0, width, height);

    input->Update();
    UpdateCamera(ImGui::GetIO().DeltaTime);

    vk::CommandBuffer cmd = nullptr;
    if (m_VulkanApi) cmd = m_VulkanApi->GetCurrentCommandBuffer();

    if (cmd) {
        // Update Scene UBO
        SceneData sceneData{};
        sceneData.view = camera->GetViewMatrix();
        sceneData.projection = camera->GetProjectionMatrix();
        sceneData.cameraPos = glm::vec4(camera->GetPosition(), 1.0f);
        sceneData.lightDir = glm::vec4(glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)), 0.0f);
        m_SceneUBO->Write(&sceneData, sizeof(SceneData));
        
        float time = ImGui::GetTime();

        if (selectedViewport == ViewportMode::Simulation3D) {
            // Compute pass (outside render pass)
            if (blackHoleRenderer) {
                 blackHoleRenderer->Render(*scene, m_meshCache, *camera, time, cmd);
            }
            
            // Transition compute output for display (must be outside render pass)
            if (blackHoleRenderer) {
                blackHoleRenderer->PrepareDisplay(cmd);
            }
            
            // Begin Render Pass
            m_VulkanApi->BeginRenderPass();
            
            // Graphics pass
            if (blackHoleRenderer) {
                blackHoleRenderer->RenderToScreen(cmd);
            }
            RenderMeshes(scene, cmd); // Render meshes on top of simulation for now, or integrate
            m_GravityGridRenderer->Render(*scene, *camera, time, cmd);
            m_ObjectPathsRenderer->Render(*scene, *camera, time, cmd);
            // m_physicsDebugRenderer->Render(scene->GetPhysicsScene()->getRenderBuffer(), camera.get(), cmd);
        } else if (selectedViewport == ViewportMode::Demo1) {
             m_VulkanApi->BeginRenderPass();
             RenderDemo1(scene, cmd);
        } else if (selectedViewport == ViewportMode::Rays2D) {
             m_VulkanApi->BeginRenderPass();
             Render2DRays(scene, cmd);
        } else if (selectedViewport == ViewportMode::HardwareRayTracing) {
             if (m_rayTracingRenderer) {
                 // Ensure renderer is resized to the viewport size (set by ViewportWindow)
                 if (m_viewportWidth > 0 && m_viewportHeight > 0) {
                     m_rayTracingRenderer->Resize((int)m_viewportWidth, (int)m_viewportHeight);
                 }
                 // Trace Rays (Updates image, transitions to ShaderReadOnlyOptimal)
                 m_rayTracingRenderer->Render(*scene, *camera, m_meshCache, cmd);
                 
                 // Begin Render Pass for UI and Final Draw
                 m_VulkanApi->BeginRenderPass();
                 
                 // Draw the raytraced image as a full-screen quad
                 // We reuse the mesh pipeline for now, using a unit plane/quad mesh if available
                 // OR we should have a dedicated quad pipeline. 
                 // For now, let's try to use the ImGui descriptor which is already created for UI
                 // But we want to draw it to the main viewport.
                 // Actually, the ViewportWindow in UI handles drawing the image via ImGui::Image
                 // So we DON'T need to draw a quad here if we are using the UI viewport!
                 // The viewport window gets the texture ID from m_rayTracingRenderer->GetImGuiDescriptorSet()
                 
                 // HOWEVER, if we are in "Game Mode" or non-UI mode, we might need to draw it.
                 // The current architecture seems to rely on ImGui for the viewport.
                 // Let's check where the viewport texture is used.
             } else {
                 m_VulkanApi->BeginRenderPass();
             }
        } else {
             // Fallback if no viewport mode selected
             m_VulkanApi->BeginRenderPass();
        }
    }
}

void Renderer::RenderMeshes(Scene* scene, vk::CommandBuffer cmd) {
    if (!scene || !camera || !m_MeshPipeline || !m_MeshPipeline->GetPipeline() || !cmd) return;
    
    m_MeshPipeline->Bind(cmd);
    
    // Bind Scene Descriptor Set
    m_MeshPipeline->BindDescriptorSet(cmd, 0, m_SceneDescriptorSet);

    for (const auto& obj : scene->objects) {
        if (!obj.HasClass("Mesh")) continue;

        ParameterHandle pathHandle("Mesh.FilePath");
        ParameterHandle posHandle("Entity.Position");
        ParameterHandle rotHandle("Entity.Rotation");
        ParameterHandle scaleHandle("Entity.Scale");

        auto pathValue = obj.GetParameter(pathHandle);
        if (!std::holds_alternative<std::string>(pathValue)) continue;
        std::string meshPath = std::get<std::string>(pathValue);

        auto mesh = GetOrLoadMesh(meshPath);
        if (mesh && mesh->IsLoaded()) {
            glm::vec3 position = obj.GetParameter<glm::vec3>(posHandle).value_or(glm::vec3(0.0f));
            glm::quat rotation = obj.GetParameter<glm::quat>(rotHandle).value_or(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            glm::vec3 scale = obj.GetParameter<glm::vec3>(scaleHandle).value_or(glm::vec3(1.0f));

            glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
            model *= glm::mat4_cast(rotation);
            model = glm::scale(model, scale);

            PushConstants pc{};
            pc.model = model;
            pc.color = glm::vec4(1.0f); // White

            m_MeshPipeline->PushConstants(cmd, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(PushConstants), &pc);
            
            mesh->Render(cmd, m_MeshPipeline->GetLayout());
        }
    }
}

// Stubs for other methods
void Renderer::DrawQuad(const glm::vec3& position, float rotationRadians, const glm::vec3& scale) {}
void Renderer::FlushQuads() {}
void Renderer::DrawCircle(const glm::vec2& pos, float radius, const glm::vec3& color) {}
void Renderer::DrawSphere(const glm::vec3& pos, float radius, const glm::vec3& color) {}
void Renderer::HandleMousePicking(Scene* scene) {}
glm::vec3 Renderer::ScreenToWorldRay(float mouseX, float mouseY, glm::vec3& rayOrigin, glm::vec3& rayDirection) { return glm::vec3(0); }
void Renderer::SetViewportBounds(float x, float y, float width, float height) { 
    m_viewportX = x; m_viewportY = y; m_viewportWidth = width; m_viewportHeight = height;
    // Defer resize to RenderScene to ensure synchronization
}
void Renderer::RenderToFramebuffer(unsigned int fbo, int width, int height, Scene* scene, Camera* cam) {}
void Renderer::SetPhysicsDebugEnabled(bool enabled) {
    if (m_physicsDebugRenderer) m_physicsDebugRenderer->SetEnabled(enabled);
}

void Renderer::SetPhysicsDebugDepthTestEnabled(bool enabled) {
    if (m_physicsDebugRenderer) m_physicsDebugRenderer->SetDepthTestEnabled(enabled);
}
void Renderer::RenderDemo1(Scene* scene, vk::CommandBuffer cmd) {}
void Renderer::Render2DRays(Scene* scene, vk::CommandBuffer cmd) {}

void Renderer::Render3DSimulation(Scene* scene, vk::CommandBuffer cmd) {
    // This function is now partially redundant as logic moved to RenderScene
    // But we can keep it for organization if needed, or remove it.
    // For now, let's leave it empty or remove calls to it.
}
void Renderer::RenderSpheres(Scene * scene, vk::CommandBuffer cmd) {}
void Renderer::InitSphereGeometry() {}

void Renderer::UpdateCamera(float deltaTime) {
    if (!camera) return;
    
    // Simple WASD movement
    float speed = 5.0f * deltaTime;
    if (input->IsKeyDown(GLFW_KEY_LEFT_SHIFT)) speed *= 2.0f;
    
    // ProcessKeyboard(forward, right, up, deltaTime)
    // We pass 1.0f for deltaTime because speed already includes it
    if (input->IsKeyDown(GLFW_KEY_W)) camera->ProcessKeyboard(speed, 0.0f, 0.0f, 1.0f);
    if (input->IsKeyDown(GLFW_KEY_S)) camera->ProcessKeyboard(-speed, 0.0f, 0.0f, 1.0f);
    if (input->IsKeyDown(GLFW_KEY_A)) camera->ProcessKeyboard(0.0f, -speed, 0.0f, 1.0f);
    if (input->IsKeyDown(GLFW_KEY_D)) camera->ProcessKeyboard(0.0f, speed, 0.0f, 1.0f);
    if (input->IsKeyDown(GLFW_KEY_Q)) camera->ProcessKeyboard(0.0f, 0.0f, -speed, 1.0f);
    if (input->IsKeyDown(GLFW_KEY_E)) camera->ProcessKeyboard(0.0f, 0.0f, speed, 1.0f);
    
    double x, y;
    input->GetMouseDelta(x, y);

    if (input->IsMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT)) {
        input->SetCursorEnabled(false);
        // User reported inverted look direction, flipping Y axis
        camera->ProcessMouse((float)x, -(float)y);
    } else {
        input->SetCursorEnabled(true);
    }
}

std::shared_ptr<GLTFMesh> Renderer::GetOrLoadMesh(const std::string& path) {
    auto it = m_meshCache.find(path);
    if (it != m_meshCache.end()) return it->second;
    
    auto mesh = std::make_shared<GLTFMesh>();
    if (mesh->Load(path, &m_VulkanApi->GetDevice(), m_TextureLayout)) {
        m_meshCache[path] = mesh;
        return mesh;
    }
    
    spdlog::error("Failed to load mesh: {}", path);
    return nullptr;
}
