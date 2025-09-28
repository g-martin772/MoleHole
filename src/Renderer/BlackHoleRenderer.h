#pragma once
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "Shader.h"
#include "Simulation/Scene.h"
#include "Camera.h"
#include "Image.h"
#include "KerrLookupTableManager.h"

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

    bool isKerrLutGenerating() const { return m_kerrLutManager.isGenerating(); }
    int getKerrLutProgress() const { return m_kerrLutManager.getGenerationProgress(); }
    void forceRegenerateKerrLuts() { m_kerrLutManager.forceRegenerateAll(); }

private:
    void CreateComputeTexture();
    void CreateFullscreenQuad();
    void LoadSkybox();
    void UpdateUniforms(const std::vector<BlackHole>& blackHoles, const Camera& camera, float time);
    void UpdateKerrLookupTables(const std::vector<BlackHole>& blackHoles);

    KerrLookupTableManager m_kerrLutManager;

    std::unique_ptr<Shader> m_computeShader;
    std::unique_ptr<Shader> m_displayShader;
    std::unique_ptr<Shader> m_kerrLutDebugShader;
    std::unique_ptr<Image> m_skyboxTexture;

    unsigned int m_computeTexture;
    unsigned int m_quadVAO, m_quadVBO;

    int m_width, m_height;

    std::vector<BlackHole> m_lastBlackHoles;
    bool m_lastKerrEnabled = false;
    int m_lastKerrResolution = 64;
    float m_lastKerrMaxDistance = 100.0f;

    static constexpr float G = 6.67430e-11f;
    static constexpr float c = 299792458.0f;
};
