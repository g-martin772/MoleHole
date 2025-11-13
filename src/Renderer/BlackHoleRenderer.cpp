#include "BlackHoleRenderer.h"
#include <glad/gl.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/type_ptr.hpp>

#include "Application/Application.h"
#include "BlackbodyLUTGenerator.h"
#include "AccelerationLUTGenerator.h"
#include "HRDiagramLUTGenerator.h"

BlackHoleRenderer::BlackHoleRenderer()
    : m_computeTexture(0), m_bloomBrightTexture(0), m_bloomBlurTexture{0, 0}, 
      m_bloomFinalTextureIndex(0),
      m_blackbodyLUT(0), m_accelerationLUT(0), m_hrDiagramLUT(0), 
      m_quadVAO(0), m_quadVBO(0), m_width(800), m_height(600) {
}

BlackHoleRenderer::~BlackHoleRenderer() {
    if (m_computeTexture) glDeleteTextures(1, &m_computeTexture);
    if (m_bloomBrightTexture) glDeleteTextures(1, &m_bloomBrightTexture);
    if (m_bloomBlurTexture[0]) glDeleteTextures(2, m_bloomBlurTexture);
    if (m_blackbodyLUT) glDeleteTextures(1, &m_blackbodyLUT);
    if (m_accelerationLUT) glDeleteTextures(1, &m_accelerationLUT);
    if (m_hrDiagramLUT) glDeleteTextures(1, &m_hrDiagramLUT);
    if (m_quadVAO) glDeleteVertexArrays(1, &m_quadVAO);
    if (m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
}

void BlackHoleRenderer::Init(int width, int height) {
    m_width = width;
    m_height = height;

    m_computeShader = std::make_unique<Shader>("../shaders/black_hole_rendering.comp", true);
    m_displayShader = std::make_unique<Shader>("../shaders/blackhole_display.vert", "../shaders/blackhole_display.frag");
    m_bloomExtractShader = std::make_unique<Shader>("../shaders/bloom_extract.comp", true);
    m_bloomBlurShader = std::make_unique<Shader>("../shaders/bloom_blur.comp", true);

    m_blackbodyLUTGenerator = std::make_unique<MoleHole::BlackbodyLUTGenerator>();
    m_accelerationLUTGenerator = std::make_unique<MoleHole::AccelerationLUTGenerator>();
    m_hrDiagramLUTGenerator = std::make_unique<MoleHole::HRDiagramLUTGenerator>();
    
    CreateComputeTexture();
    CreateBloomTextures();
    CreateFullscreenQuad();
    LoadSkybox();
    GenerateBlackbodyLUT();
    GenerateAccelerationLUT();
    GenerateHRDiagramLUT();

    spdlog::info("BlackHoleRenderer initialized with {}x{} resolution", width, height);
}

void BlackHoleRenderer::LoadSkybox() {
    const std::string defaultPath = "../assets/space.hdr";
    m_skyboxTexture = std::unique_ptr<Image>(Image::LoadHDR(defaultPath));
}

void BlackHoleRenderer::GenerateBlackbodyLUT() {
    using namespace MoleHole;
    
    spdlog::info("Generating blackbody LUT ({}x{})...", 
                 m_blackbodyLUTGenerator->LUT_WIDTH, 
                 m_blackbodyLUTGenerator->LUT_HEIGHT);
    
    // Generate the LUT data
    auto lutData = m_blackbodyLUTGenerator->generateLUT();
    
    // Create and upload the OpenGL texture
    if (m_blackbodyLUT) {
        glDeleteTextures(1, &m_blackbodyLUT);
    }
    
    glGenTextures(1, &m_blackbodyLUT);
    glBindTexture(GL_TEXTURE_2D, m_blackbodyLUT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, 
                 BlackbodyLUTGenerator::LUT_WIDTH, 
                 BlackbodyLUTGenerator::LUT_HEIGHT, 
                 0, GL_RGB, GL_FLOAT, lutData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    spdlog::info("Blackbody LUT generated successfully");
}

void BlackHoleRenderer::GenerateAccelerationLUT() {
    using namespace MoleHole;
    
    spdlog::info("Generating acceleration LUT ({}x{})...", 
                 AccelerationLUTGenerator::LUT_WIDTH, 
                 AccelerationLUTGenerator::LUT_HEIGHT);
    
    // Generate the LUT data
    auto lutData = AccelerationLUTGenerator::generateLUT();
    
    // Create and upload the OpenGL texture
    if (m_accelerationLUT) {
        glDeleteTextures(1, &m_accelerationLUT);
    }
    
    glGenTextures(1, &m_accelerationLUT);
    glBindTexture(GL_TEXTURE_2D, m_accelerationLUT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 
                 AccelerationLUTGenerator::LUT_WIDTH, 
                 AccelerationLUTGenerator::LUT_HEIGHT, 
                 0, GL_RED, GL_FLOAT, lutData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    spdlog::info("Acceleration LUT generated successfully");
}

void BlackHoleRenderer::GenerateHRDiagramLUT() {
    using namespace MoleHole;
    
    spdlog::info("Generating HR diagram LUT ({} samples)...", 
                 HRDiagramLUTGenerator::LUT_SIZE);
    
    // Generate the LUT data
    auto lutData = HRDiagramLUTGenerator::generateLUT();
    
    // Create and upload the OpenGL texture (1D texture for mass lookup)
    if (m_hrDiagramLUT) {
        glDeleteTextures(1, &m_hrDiagramLUT);
    }
    
    glGenTextures(1, &m_hrDiagramLUT);
    glBindTexture(GL_TEXTURE_2D, m_hrDiagramLUT);
    // Use 2D texture with height=1 for compatibility
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, 
                 HRDiagramLUTGenerator::LUT_SIZE, 
                 1, 
                 0, GL_RGB, GL_FLOAT, lutData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    spdlog::info("HR diagram LUT generated successfully");
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

void BlackHoleRenderer::CreateBloomTextures() {
    // Delete existing textures if they exist
    if (m_bloomBrightTexture) {
        glDeleteTextures(1, &m_bloomBrightTexture);
    }
    if (m_bloomBlurTexture[0]) {
        glDeleteTextures(2, m_bloomBlurTexture);
    }

    // Create bright extraction texture
    glGenTextures(1, &m_bloomBrightTexture);
    glBindTexture(GL_TEXTURE_2D, m_bloomBrightTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Create ping-pong textures for blur
    for (int i = 0; i < 2; i++) {
        glGenTextures(1, &m_bloomBlurTexture[i]);
        glBindTexture(GL_TEXTURE_2D, m_bloomBlurTexture[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
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
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
}

void BlackHoleRenderer::Render(const std::vector<BlackHole>& blackHoles, const std::vector<Sphere>& spheres, const Camera& camera, float time) {
    UpdateUniforms(blackHoles, spheres, camera, time);

    m_computeShader->Bind();

    glBindImageTexture(0, m_computeTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    // Bind skybox texture to unit 1
    if (m_skyboxTexture) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_skyboxTexture->textureID);
        m_computeShader->SetInt("u_skyboxTexture", 1);
    }
    
    // Bind blackbody LUT to unit 2
    if (m_blackbodyLUT) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_blackbodyLUT);
        m_computeShader->SetInt("u_blackbodyLUT", 2);
        
        // Set LUT parameters (must match the generator constants)
        m_computeShader->SetFloat("u_lutTempMin", 1000.0f);
        m_computeShader->SetFloat("u_lutTempMax", 40000.0f);
        m_computeShader->SetFloat("u_lutRedshiftMin", 0.1f);
        m_computeShader->SetFloat("u_lutRedshiftMax", 3.0f);
        m_computeShader->SetInt("u_useBlackbodyLUT", 1);
    }

    // Bind acceleration LUT to unit 3
    if (m_accelerationLUT) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_accelerationLUT);
        m_computeShader->SetInt("u_accelerationLUT", 3);
        m_computeShader->SetInt("u_useAccelerationLUT", 1);
    }

    // Bind HR diagram LUT to unit 4
    if (m_hrDiagramLUT) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, m_hrDiagramLUT);
        m_computeShader->SetInt("u_hrDiagramLUT", 4);
        m_computeShader->SetInt("u_useHRDiagramLUT", 1);
    }

    auto& config = Application::State();

    if (!blackHoles.empty()) {
        m_computeShader->SetInt("u_debugMode", static_cast<int>(config.rendering.debugMode));
    }

    unsigned int groupsX = (m_width + 15) / 16;
    unsigned int groupsY = (m_height + 15) / 16;
    m_computeShader->Dispatch(groupsX, groupsY, 1);

    // Ensure writes to the image are visible to subsequent texture fetches in the fragment shader
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    m_computeShader->Unbind();
    
    // Apply bloom effect
    ApplyBloom();
}

void BlackHoleRenderer::ApplyBloom() {
    auto& config = Application::State();
    
    // Check if bloom is enabled
    int bloomEnabled = config.GetProperty<int>("bloomEnabled", 1);
    if (!bloomEnabled) {
        return;
    }
    
    unsigned int groupsX = (m_width + 15) / 16;
    unsigned int groupsY = (m_height + 15) / 16;
    
    // Step 1: Extract bright areas
    m_bloomExtractShader->Bind();
    
    float bloomThreshold = config.GetProperty<float>("bloomThreshold", 1.0f);
    m_bloomExtractShader->SetFloat("u_bloomThreshold", bloomThreshold);
    
    glBindImageTexture(0, m_computeTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, m_bloomBrightTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    
    m_bloomExtractShader->Unbind();
    
    // Step 2: Apply Gaussian blur (ping-pong between two textures)
    m_bloomBlurShader->Bind();
    
    int blurPasses = config.GetProperty<int>("bloomBlurPasses", 4);
    
    bool horizontal = true;
    unsigned int srcTexture = m_bloomBrightTexture;
    
    for (int i = 0; i < blurPasses * 2; i++) {
        m_bloomBlurShader->SetInt("u_horizontal", horizontal ? 1 : 0);
        
        int dstIndex = horizontal ? 0 : 1;
        
        glBindImageTexture(0, srcTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(1, m_bloomBlurTexture[dstIndex], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        
        glDispatchCompute(groupsX, groupsY, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        
        srcTexture = m_bloomBlurTexture[dstIndex];
        m_bloomFinalTextureIndex = dstIndex;
        horizontal = !horizontal;
    }
    
    m_bloomBlurShader->Unbind();
    
    // Ensure bloom texture writes are visible to texture fetches in fragment shader
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
}

void BlackHoleRenderer::UpdateUniforms(const std::vector<BlackHole>& blackHoles, const std::vector<Sphere>& spheres, const Camera& camera, float time) {
    m_computeShader->Bind();

    auto& config = Application::State();

    glm::vec3 cameraPos = camera.GetPosition();
    glm::vec3 cameraFront = camera.GetFront();
    glm::vec3 cameraUp = camera.GetUp();
    glm::vec3 cameraRight = glm::normalize(glm::cross(cameraFront, cameraUp));

    m_computeShader->SetVec3("u_cameraPos", cameraPos);
    m_computeShader->SetVec3("u_cameraFront", cameraFront);
    m_computeShader->SetVec3("u_cameraUp", cameraUp);
    m_computeShader->SetVec3("u_cameraRight", cameraRight);
    m_computeShader->SetFloat("u_fov", camera.GetFov());
    m_computeShader->SetFloat("u_aspect", static_cast<float>(m_width) / static_cast<float>(m_height));
    m_computeShader->SetFloat("u_time", time);

    // Rendering settings from AppState
    m_computeShader->SetInt("u_gravitationalLensingEnabled", config.GetProperty<int>("gravitationalLensingEnabled", 1));
    m_computeShader->SetInt("u_accretionDiskEnabled", config.GetProperty<int>("accretionDiskEnabled", 1));
    m_computeShader->SetInt("u_renderBlackHoles", config.GetProperty<int>("renderBlackHoles", 1));
    m_computeShader->SetFloat("u_accDiskHeight", config.GetProperty<float>("accDiskHeight", 0.2f));
    m_computeShader->SetFloat("u_accDiskNoiseScale", config.GetProperty<float>("accDiskNoiseScale", 1.0f));
    m_computeShader->SetFloat("u_accDiskNoiseLOD", config.GetProperty<float>("accDiskNoiseLOD", 5.0f));
    m_computeShader->SetFloat("u_accDiskSpeed", config.GetProperty<float>("accDiskSpeed", 0.5f));
    m_computeShader->SetFloat("u_dopplerBeamingEnabled", config.GetProperty<float>("dopplerBeamingEnabled", 1.0f));
    m_computeShader->SetFloat("u_accDiskTemp", config.GetProperty<float>("accDiskTemp", 2000.0f));
    m_computeShader->SetInt("u_gravitationalRedshiftEnabled", config.GetProperty<int>("gravitationalRedshiftEnabled", 1));

    // Black holes
    int numBlackHoles = std::min(static_cast<int>(blackHoles.size()), 8);
    m_computeShader->SetInt("u_numBlackHoles", numBlackHoles);

    for (int i = 0; i < numBlackHoles; i++) {
        const BlackHole& bh = blackHoles[i];

        // Convert mass to appropriate units for shader (using solar masses as base unit)
        float normalizedMass = bh.mass; // Assume mass is already in appropriate units, whatever these are like pls help

        std::string posUniform = "u_blackHolePositions[" + std::to_string(i) + "]";
        std::string massUniform = "u_blackHoleMasses[" + std::to_string(i) + "]";

        m_computeShader->SetVec3(posUniform, bh.position);
        m_computeShader->SetFloat(massUniform, normalizedMass);
    }
    
    // Spheres
    int renderSpheres = config.GetProperty<int>("renderSpheresInRayMarching", 1);
    m_computeShader->SetInt("u_renderSpheres", renderSpheres);
    
    int numSpheres = std::min(static_cast<int>(spheres.size()), 16); // MAX_SPHERES = 16
    m_computeShader->SetInt("u_numSpheres", numSpheres);
    
    for (int i = 0; i < numSpheres; i++) {
        const Sphere& sphere = spheres[i];
        
        std::string posUniform = "u_spherePositions[" + std::to_string(i) + "]";
        std::string radiusUniform = "u_sphereRadii[" + std::to_string(i) + "]";
        std::string colorUniform = "u_sphereColors[" + std::to_string(i) + "]";
        std::string massUniform = "u_sphereMasses[" + std::to_string(i) + "]";
        
        m_computeShader->SetVec3(posUniform, sphere.position);
        m_computeShader->SetFloat(radiusUniform, sphere.radius);
        m_computeShader->SetVec4(colorUniform, sphere.color);
        
        // Convert mass from kg to solar masses (1 solar mass = 1.989e30 kg)
        float massInSolarMasses = sphere.massKg / 1.989e30f;
        m_computeShader->SetFloat(massUniform, massInSolarMasses);
    }

    m_computeShader->Unbind();
}

void BlackHoleRenderer::RenderToScreen() {
    auto& config = Application::State();
    
    m_displayShader->Bind();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_computeTexture);
    m_displayShader->SetInt("u_raytracedImage", 0);

    // Bind bloom texture
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_bloomBlurTexture[m_bloomFinalTextureIndex]); // Use final blur result
    m_displayShader->SetInt("u_bloomImage", 1);
    
    // Set bloom parameters
    int bloomEnabled = config.GetProperty<int>("bloomEnabled", 1);
    float bloomIntensity = config.GetProperty<float>("bloomIntensity", 1.0f);
    int bloomDebug = config.GetProperty<int>("bloomDebug", 0);
    m_displayShader->SetInt("u_bloomEnabled", bloomEnabled);
    m_displayShader->SetFloat("u_bloomIntensity", bloomIntensity);
    m_displayShader->SetInt("u_bloomDebug", bloomDebug);

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    m_displayShader->Unbind();
}

void BlackHoleRenderer::Resize(int width, int height) {
    m_width = width;
    m_height = height;
    CreateComputeTexture();
    CreateBloomTextures();
}