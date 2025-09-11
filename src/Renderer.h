#pragma once
#include "Shader.h"
#include <memory>

#include "Scene.h"
#include "Image.h"

struct GLFWwindow;

class Renderer {
public:
    void Init();
    void Shutdown();
    void BeginFrame();
    void EndFrame();
    void RenderDockspace();
    void RenderUI();
    void RenderScene(Scene *scene);

    GLFWwindow* GetWindow() const { return window; }
private:
    GLFWwindow* window = nullptr;
    int last_img_width = 800;
    int last_img_height = 600;
    std::shared_ptr<Image> image;
    std::unique_ptr<Shader> triangleShader;
    std::unique_ptr<Shader> circleShader;
    std::unique_ptr<Shader> gradientShader;
    unsigned int triangleVAO = 0, triangleVBO = 0;
    unsigned int quadVAO = 0, quadVBO = 0;
};
