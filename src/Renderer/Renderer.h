#pragma once
#include "Shader.h"
#include "BlackHoleRenderer.h"
#include <memory>
#include <glm/glm.hpp>
#include <vector>

#include "Simulation/Scene.h"
#include "Image.h"
#include "Camera.h"
#include "Input.h"
#include "GravityGridRenderer.h"
#include "VisualRenderer.h"

struct GLFWwindow;

class Renderer {
public:
    enum class ViewportMode {
        Demo1 = 0,
        Rays2D = 1,
        Simulation3D = 2,
        SimulationVisual = 3,
    };
    void Init();
    void Shutdown();
    void BeginFrame();
    void EndFrame();
    void RenderScene(Scene *scene);

    ~Renderer() = default;
    void DrawQuad(const glm::vec3& position, float rotationRadians = 0.0f, const glm::vec3& scale = glm::vec3(1.0f));
    void FlushQuads();

    void DrawCircle(const glm::vec2& pos, float radius, const glm::vec3& color);
    void DrawSphere(const glm::vec3& pos, float radius, const glm::vec3& color);

    void HandleMousePicking(Scene* scene);
    glm::vec3 ScreenToWorldRay(float mouseX, float mouseY, glm::vec3& rayOrigin, glm::vec3& rayDirection);
    void SetViewportBounds(float x, float y, float width, float height);

    GLFWwindow* GetWindow() const { return window; }

    ViewportMode GetSelectedViewport() const { return selectedViewport; }
    void SetSelectedViewport(ViewportMode mode) { selectedViewport = mode; }

    GravityGridRenderer* GetGravityGridRenderer() { return gravityGridRenderer.get(); }

    GLFWwindow* window = nullptr;
    int last_img_width = 800;
    int last_img_height = 600;
    std::shared_ptr<Image> image;
    std::unique_ptr<Shader> quadShader;
    std::unique_ptr<Shader> circleShader;
    std::unique_ptr<Shader> sphereShader;
    std::unique_ptr<BlackHoleRenderer> blackHoleRenderer;
    std::unique_ptr<VisualRenderer> visualRenderer;
    std::unique_ptr<Camera> camera;
    std::unique_ptr<Input> input;
    int lastVsync = -1;
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

private:
    void RenderDemo1(Scene* scene);
    void Render2DRays(Scene* scene);
    void Render3DSimulation(Scene *scene);
    void RenderVisualSimulation(Scene *scene);

    std::unique_ptr<GravityGridRenderer> gravityGridRenderer;
};
