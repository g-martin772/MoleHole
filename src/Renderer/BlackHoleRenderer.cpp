#include "BlackHoleRenderer.h"
#include <glad/gl.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Application/Application.h"

BlackHoleRenderer::BlackHoleRenderer()
    : m_computeTexture(0), m_quadVAO(0), m_quadVBO(0), m_width(800), m_height(600) {
}

BlackHoleRenderer::~BlackHoleRenderer() {
    if (m_computeTexture) glDeleteTextures(1, &m_computeTexture);
    if (m_quadVAO) glDeleteVertexArrays(1, &m_quadVAO);
    if (m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
}

void BlackHoleRenderer::Init(int width, int height) {
    m_width = width;
    m_height = height;

    m_computeShader = std::make_unique<Shader>("../shaders/blackhole_raytrace.comp", true);
    m_displayShader = std::make_unique<Shader>("../shaders/blackhole_display.vert", "../shaders/blackhole_display.frag");
    m_kerrLutDebugShader = std::make_unique<Shader>("../shaders/blackhole_display.vert", "../shaders/kerr_lut_debug.frag");

    CreateComputeTexture();
    CreateFullscreenQuad();
    LoadSkybox();

    spdlog::info("BlackHoleRenderer initialized with {}x{} resolution", width, height);

    m_kerrLutManager.initialize(m_lastKerrResolution, m_lastKerrMaxDistance);
}

void BlackHoleRenderer::LoadSkybox() {
    m_skyboxTexture = std::unique_ptr<Image>(Image::LoadHDR("../assets/space.hdr"));
    if (!m_skyboxTexture) {
        spdlog::error("Failed to load skybox texture");
    }
}

void BlackHoleRenderer::CreateComputeTexture() {
    if (m_computeTexture) {
        glDeleteTextures(1, &m_computeTexture);
    }

    glGenTextures(1, &m_computeTexture);
    glBindTexture(GL_TEXTURE_2D, m_computeTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindImageTexture(0, m_computeTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
}

void BlackHoleRenderer::CreateFullscreenQuad() {
    float quadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);

    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
}

void BlackHoleRenderer::Render(const std::vector<BlackHole>& blackHoles, const Camera& camera, float time) {
    UpdateUniforms(blackHoles, camera, time);

    m_computeShader->Bind();

    glBindImageTexture(0, m_computeTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    if (m_skyboxTexture) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_skyboxTexture->textureID);
        m_computeShader->SetInt("u_skyboxTexture", 1);
    }

    auto& config = Application::State();

    if (config.rendering.enableDistortion && !blackHoles.empty()) {
        GLuint kerrLut = m_kerrLutManager.getCurrentLookupTable();
        if (kerrLut != 0) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_3D, kerrLut);
            m_computeShader->SetInt("u_kerrLookupTable", 2);
        }

        m_computeShader->SetInt("u_useKerrDistortion", 1);
        m_computeShader->SetFloat("u_kerrLutMaxDistance", config.rendering.kerrMaxDistance);
        m_computeShader->SetInt("u_kerrLutResolution", config.rendering.kerrLutResolution);
        m_computeShader->SetInt("u_debugMode", static_cast<int>(config.rendering.debugMode));
    } else {
        m_computeShader->SetInt("u_useKerrDistortion", 0);
        m_computeShader->SetInt("u_debugMode", 0);
    }

    unsigned int groupsX = (m_width + 15) / 16;
    unsigned int groupsY = (m_height + 15) / 16;
    m_computeShader->Dispatch(groupsX, groupsY, 1);

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    m_computeShader->Unbind();
}

void BlackHoleRenderer::UpdateUniforms(const std::vector<BlackHole>& blackHoles, const Camera& camera, float time) {
    m_computeShader->Bind();

    glm::vec3 cameraPos = camera.GetPosition();
    glm::mat4 viewMatrix = camera.GetViewMatrix();

    glm::vec3 cameraFront = -glm::vec3(viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2]);
    glm::vec3 cameraUp = glm::vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
    glm::vec3 cameraRight = glm::vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);

    m_computeShader->SetVec3("u_cameraPos", cameraPos);
    m_computeShader->SetVec3("u_cameraFront", cameraFront);
    m_computeShader->SetVec3("u_cameraUp", cameraUp);
    m_computeShader->SetVec3("u_cameraRight", cameraRight);
    m_computeShader->SetFloat("u_fov", 45.0f); // TODO: Get from camera
    m_computeShader->SetFloat("u_aspect", (float)m_width / (float)m_height);
    m_computeShader->SetFloat("u_time", time);

    int numBlackHoles = std::min((int)blackHoles.size(), 8);
    m_computeShader->SetInt("u_numBlackHoles", numBlackHoles);

    for (int i = 0; i < numBlackHoles; i++) {
        const BlackHole& bh = blackHoles[i];

        // Convert mass to appropriate units for shader (using solar masses as base unit)
        float normalizedMass = bh.mass; // Assume mass is already in appropriate units, whatever these are like pls help

        std::string posUniform = "u_blackHolePositions[" + std::to_string(i) + "]";
        std::string massUniform = "u_blackHoleMasses[" + std::to_string(i) + "]";
        std::string spinUniform = "u_blackHoleSpins[" + std::to_string(i) + "]";
        std::string spinAxisUniform = "u_blackHoleSpinAxes[" + std::to_string(i) + "]";
        std::string diskShowUniform = "u_showAccretionDisks[" + std::to_string(i) + "]";
        std::string diskSizeUniform = "u_accretionDiskSizes[" + std::to_string(i) + "]";
        std::string diskColorUniform = "u_accretionDiskColors[" + std::to_string(i) + "]";

        m_computeShader->SetVec3(posUniform, bh.position);
        m_computeShader->SetFloat(massUniform, normalizedMass);
        m_computeShader->SetFloat(spinUniform, bh.spin);
        m_computeShader->SetVec3(spinAxisUniform, bh.spinAxis);
        m_computeShader->SetInt(diskShowUniform, bh.showAccretionDisk ? 1 : 0);
        m_computeShader->SetFloat(diskSizeUniform, bh.accretionDiskSize);
        m_computeShader->SetVec3(diskColorUniform, bh.accretionDiskColor);
    }

    m_computeShader->Unbind();
}

void BlackHoleRenderer::RenderToScreen() {
    m_displayShader->Bind();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_computeTexture);
    m_displayShader->SetInt("u_raytracedImage", 0);

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    m_displayShader->Unbind();

    auto& config = Application::State();

    if (!config.rendering.enableDistortion || config.rendering.debugMode != DebugMode::DebugLUT) return;

    GLuint kerrLut = m_kerrLutManager.getCurrentLookupTable();
    if (kerrLut == 0 || !m_kerrLutDebugShader) return;

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    int overlaySize = 256;
    int x = 10;
    int y = prevViewport[3] - overlaySize - 10;
    glViewport(x, y, overlaySize, overlaySize);

    m_kerrLutDebugShader->Bind();

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, kerrLut);
    m_kerrLutDebugShader->SetInt("u_kerrLut", 3);
    m_kerrLutDebugShader->SetFloat("u_sliceX", 0.5f);

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    m_kerrLutDebugShader->Unbind();

    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

void BlackHoleRenderer::Resize(int width, int height) {
    m_width = width;
    m_height = height;
    CreateComputeTexture();
}

float BlackHoleRenderer::CalculateSchwarzschildRadius(float mass) {
    // rs = 2GM/c²
    // For solar mass units: rs ≈ 2.95 km per solar mass
    const float solarMassInKg = 1.989e30f;
    float massInKg = mass * solarMassInKg;
    return (2.0f * G * massInKg) / (c * c);
}

float BlackHoleRenderer::GetEventHorizonRadius(float mass) {
    return CalculateSchwarzschildRadius(mass);
}

void BlackHoleRenderer::UpdateKerrLookupTables(const std::vector<BlackHole>& blackHoles) {
    // if (!globalOptions->kerrDistortionEnabled || blackHoles.empty()) {
    //     return;
    // }
    //
    // const BlackHole& primaryBH = blackHoles[0];
    //
    // bool globalOptionsChanged = (globalOptions->kerrDistortionEnabled != m_lastKerrEnabled ||
    //                             globalOptions->kerrLutResolution != m_lastKerrResolution ||
    //                             globalOptions->kerrMaxDistance != m_lastKerrMaxDistance);
    //
    // bool blackHoleChanged = false;
    // if (m_lastBlackHoles.empty() ||
    //     glm::distance(primaryBH.position, m_lastBlackHoles[0].position) > 0.1f ||
    //     std::abs(primaryBH.mass - m_lastBlackHoles[0].mass) > 0.01f ||
    //     std::abs(primaryBH.spin - m_lastBlackHoles[0].spin) > 0.01f) {
    //     blackHoleChanged = true;
    // }
    //
    // bool needsUpdate = globalOptionsChanged || blackHoleChanged;
    //
    // if (globalOptionsChanged) {
    //     m_kerrLutManager.setLutResolution(globalOptions->kerrLutResolution);
    //     m_kerrLutManager.setMaxDistance(globalOptions->kerrMaxDistance);
    //
    //     m_lastKerrEnabled = globalOptions->kerrDistortionEnabled;
    //     m_lastKerrResolution = globalOptions->kerrLutResolution;
    //     m_lastKerrMaxDistance = globalOptions->kerrMaxDistance;
    // }
    //
    // if (needsUpdate || m_kerrLutManager.needsRegeneration(primaryBH)) {
    //     m_kerrLutManager.getLookupTable(primaryBH);
    //
    //     m_lastBlackHoles.clear();
    //     m_lastBlackHoles.push_back(primaryBH);
    //
    //     spdlog::info("Generated Kerr lookup table for black hole at ({:.2f}, {:.2f}, {:.2f}) with mass {:.2f} and spin {:.2f}",
    //                  primaryBH.position.x, primaryBH.position.y, primaryBH.position.z, primaryBH.mass, primaryBH.spin);
    // }
}
