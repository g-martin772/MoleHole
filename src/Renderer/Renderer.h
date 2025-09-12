#pragma once
#include "Shader.h"
#include "BlackHoleRenderer.h"
#include <memory>
#include <glm/glm.hpp>
#include <vector>

#include "Scene.h"
#include "Image.h"
#include "Camera.h"
#include "Input.h"
#include "GlobalOptions.h"

struct GLFWwindow;

class Renderer {
public:
    enum class ViewportMode {
        Demo1 = 0,
        Rays2D = 1,
        Simulation3D = 2
    };
    void Init(GlobalOptions* options);
    void Shutdown();
    void BeginFrame();
    void EndFrame();
    void RenderDockspace(Scene* scene);
    void RenderUI(float fps, Scene *scene);
    void RenderScene(Scene *scene);

    void DrawQuad(const glm::vec3& position, float rotationRadians = 0.0f, const glm::vec3& scale = glm::vec3(1.0f));
    void FlushQuads();

    void DrawCircle(const glm::vec2& pos, float radius, const glm::vec3& color);
    void DrawSphere(const glm::vec3& pos, float radius, const glm::vec3& color);

    GLFWwindow* GetWindow() const { return window; }
    GLFWwindow* window = nullptr;
    int last_img_width = 800;
    int last_img_height = 600;
    std::shared_ptr<Image> image;
    std::unique_ptr<Shader> quadShader;
    std::unique_ptr<Shader> circleShader;
    std::unique_ptr<Shader> sphereShader;
    std::unique_ptr<BlackHoleRenderer> blackHoleRenderer;
    std::unique_ptr<Camera> camera;
    std::unique_ptr<Input> input;
    GlobalOptions* globalOptions = nullptr;
    int lastVsync = -1;
    void UpdateCamera(float deltaTime);

    struct QuadInstance {
        glm::vec3 position;
        float rotation;
        glm::vec3 scale;
    };
    std::vector<QuadInstance> quadInstances;

    ViewportMode selectedViewport = ViewportMode::Simulation3D;
    void RenderDemo1(Scene* scene);
    void Render2DRays(Scene* scene);
    void Render3DSimulation(Scene *scene);
};
