//
// Created by leo on 10/7/25.
//

#pragma once
#include <memory>

#include "Camera.h"
#include "Shader.h"

class VisualRenderer {
public:
    VisualRenderer();
    ~VisualRenderer();

    void Init(int width, int height);
    void Render(const Camera& camera, float time);

    void CreateFullscreenQuad();
    uint32_t loadTexture2D(const std::string &file, bool repeat = true);
    uint32_t loadCubemap(const std::string &cubemapDir);

private:
    std::unique_ptr<Shader> m_displayShader;
    uint32_t galaxy, colorMap;
    unsigned int m_quadVAO, m_quadVBO;
    int m_width, m_height;
};
