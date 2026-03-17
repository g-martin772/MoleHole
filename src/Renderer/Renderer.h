#pragma once
// #include "Interface/Shader.h"
#include "Modules/BlackHoleRenderer.h"
#include "Models/GLTFMesh.h"
#include "Application/Window.h"
// #include "Interface/Image.h"
#include "Interface/Camera.h"
#include "Interface/Input.h"
#include "Modules/GravityGridRenderer.h"
#include "Modules/ObjectPathsRenderer.h"
#include "Modules/PhysicsDebugRenderer.h"
#include "Platform/Vulkan/VulkanApi.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Platform/Vulkan/VulkanShader.h"

class Renderer {
public:
    enum class ViewportMode {
        Demo1 = 0,
        Rays2D = 1,
        Simulation3D = 2
    };
    void Init();
    void Init(bool headless);
    void Shutdown();
    void BeginFrame();
    void EndFrame(bool clearScreen = true);
    void RenderScene(Scene *scene);

    ~Renderer() = default;
    void DrawQuad(const glm::vec3& position, float rotationRadians = 0.0f, const glm::vec3& scale = glm::vec3(1.0f));
    void FlushQuads();

    void DrawCircle(const glm::vec2& pos, float radius, const glm::vec3& color);
    void DrawSphere(const glm::vec3& pos, float radius, const glm::vec3& color);

    void HandleMousePicking(Scene* scene);
    glm::vec3 ScreenToWorldRay(float mouseX, float mouseY, glm::vec3& rayOrigin, glm::vec3& rayDirection);
    void SetViewportBounds(float x, float y, float width, float height);

    void RenderToFramebuffer(unsigned int fbo, int width, int height, Scene* scene, Camera* cam);

    Window* GetWindowAbstraction() const { return m_window; }

    ViewportMode GetSelectedViewport() const { return selectedViewport; }
    void SetSelectedViewport(ViewportMode mode) { selectedViewport = mode; }

    GravityGridRenderer* GetGravityGridRenderer() const { return m_GravityGridRenderer.get(); }
    ObjectPathsRenderer* GetObjectPathsRenderer() const { return m_ObjectPathsRenderer.get(); }
    PhysicsDebugRenderer* GetPhysicsDebugRenderer() const { return m_physicsDebugRenderer.get(); }

    void SetPhysicsDebugEnabled(bool enabled);
    void SetPhysicsDebugDepthTestEnabled(bool enabled);

    std::unique_ptr<VulkanApi> m_VulkanApi;
    std::unique_ptr<VulkanPipeline> m_MeshPipeline;
    std::unique_ptr<VulkanShader> m_MeshShader;
    std::unique_ptr<VulkanBuffer> m_SceneUBO;
    
    vk::DescriptorPool m_DescriptorPool;
    vk::DescriptorSet m_SceneDescriptorSet;
    vk::DescriptorSetLayout m_TextureLayout;
    
    Window* m_window = nullptr;
    int last_img_width = 800;
    int last_img_height = 600;
    // std::shared_ptr<Image> image;
    // std::unique_ptr<Shader> quadShader;
    // std::unique_ptr<Shader> circleShader;
    // std::unique_ptr<Shader> sphereShader;
    std::unique_ptr<Camera> camera;
    std::unique_ptr<Input> input;
    bool m_vsync = true;
    void UpdateCamera(float deltaTime);

    struct QuadInstance {
        glm::vec3 position;
        float rotation;
        glm::vec3 scale;
    };
    std::vector<QuadInstance> quadInstances;

    ViewportMode selectedViewport = ViewportMode::Simulation3D;

    float m_viewportX = 0.0f;
    float m_viewportY = 0.0f;
    float m_viewportWidth = 800.0f;
    float m_viewportHeight = 600.0f;

    std::unordered_map<std::string, std::shared_ptr<GLTFMesh>> m_meshCache;

private:
    void RenderDemo1(Scene* scene, vk::CommandBuffer cmd);
    void Render2DRays(Scene* scene, vk::CommandBuffer cmd);
    void Render3DSimulation(Scene *scene, vk::CommandBuffer cmd);
    void RenderMeshes(Scene* scene, vk::CommandBuffer cmd);
    void RenderSpheres(Scene * scene, vk::CommandBuffer cmd);
    std::shared_ptr<GLTFMesh> GetOrLoadMesh(const std::string& path);

    void InitSphereGeometry();
    unsigned int m_SphereVAO = 0;
    unsigned int m_SphereVBO = 0;
    unsigned int m_SphereEBO = 0;
    int m_SphereIndexCount = 0;

    std::unique_ptr<BlackHoleRenderer> blackHoleRenderer;
    std::unique_ptr<GravityGridRenderer> m_GravityGridRenderer;
    std::unique_ptr<ObjectPathsRenderer> m_ObjectPathsRenderer;
    std::unique_ptr<PhysicsDebugRenderer> m_physicsDebugRenderer;
};
