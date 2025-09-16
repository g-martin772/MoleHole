#pragma once
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "Shader.h"
#include "Scene.h"
#include "Camera.h"
#include "Image.h"

class BlackHoleRenderer {
public:
    BlackHoleRenderer();
    ~BlackHoleRenderer();

    void Init(int width, int height);
    void Render(const std::vector<BlackHole>& blackHoles, const Camera& camera, float time);
    void Resize(int width, int height);
    void RenderToScreen();

    static float CalculateSchwarzschildRadius(float mass);
    static float GetEventHorizonRadius(float mass);

private:
    void CreateComputeTexture();
    void CreateFullscreenQuad();
    void LoadSkybox();
    void UpdateUniforms(const std::vector<BlackHole>& blackHoles, const Camera& camera, float time);

    std::unique_ptr<Shader> m_computeShader;
    std::unique_ptr<Shader> m_displayShader;
    std::unique_ptr<Image> m_skyboxTexture;

    unsigned int m_computeTexture;
    unsigned int m_quadVAO, m_quadVBO;

    int m_width, m_height;

    static constexpr float G = 6.67430e-11f;
    static constexpr float c = 299792458.0f;
};
