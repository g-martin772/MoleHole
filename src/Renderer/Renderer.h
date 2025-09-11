#pragma once
#include "Shader.h"
#include <memory>

#include "../Scene.h"
#include "Image.h"
#include "Camera.h"
#include "Input.h"
#include "../GlobalOptions.h"

struct GLFWwindow;

class Renderer {
public:
    void Init(GlobalOptions* options);
    void Shutdown();
    void BeginFrame();
    void EndFrame();
    void RenderDockspace();
    void RenderUI(float fps);
    void RenderScene(Scene *scene);

    GLFWwindow* GetWindow() const { return window; }
private:
    GLFWwindow* window = nullptr;
    int last_img_width = 800;
    int last_img_height = 600;
    std::shared_ptr<Image> image;
    std::unique_ptr<Shader> quadShader;
    std::unique_ptr<Camera> camera;
    std::unique_ptr<Input> input;
    GlobalOptions* globalOptions = nullptr;
    int lastVsync = -1;
    void UpdateCamera(float deltaTime);
    void DrawQuad();
};
