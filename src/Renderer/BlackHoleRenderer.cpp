#include "BlackHoleRenderer.h"
#include <glad/gl.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/type_ptr.hpp>

#include "Application/Application.h"
#include "Application/Parameters.h"
#include "BlackbodyLUTGenerator.h"
#include "AccelerationLUTGenerator.h"
#include "HRDiagramLUTGenerator.h"
#include "GLTFMesh.h"

BlackHoleRenderer::BlackHoleRenderer()
    : m_computeTexture(0), m_bloomBrightTexture(0), m_bloomBlurTexture{0, 0}, 
      m_bloomFinalTextureIndex(0), m_lensFlareTexture(0),
      m_blackbodyLUT(0), m_accelerationLUT(0), m_hrDiagramLUT(0),
      m_kerrDeflectionLUT(0), m_kerrRedshiftLUT(0), m_kerrPhotonSphereLUT(0), m_kerrISCOLUT(0),
      m_quadVAO(0), m_quadVBO(0), m_meshDataSSBO(0), m_triangleSSBO(0),
      m_width(800), m_height(600) {
}

BlackHoleRenderer::~BlackHoleRenderer() {
    if (m_computeTexture) glDeleteTextures(1, &m_computeTexture);
    if (m_bloomBrightTexture) glDeleteTextures(1, &m_bloomBrightTexture);
    if (m_bloomBlurTexture[0]) glDeleteTextures(2, m_bloomBlurTexture);
    if (m_lensFlareTexture) glDeleteTextures(1, &m_lensFlareTexture);
    if (m_blackbodyLUT) glDeleteTextures(1, &m_blackbodyLUT);
    if (m_accelerationLUT) glDeleteTextures(1, &m_accelerationLUT);
    if (m_hrDiagramLUT) glDeleteTextures(1, &m_hrDiagramLUT);
    if (m_kerrDeflectionLUT) glDeleteTextures(1, &m_kerrDeflectionLUT);
    if (m_kerrRedshiftLUT) glDeleteTextures(1, &m_kerrRedshiftLUT);
    if (m_kerrPhotonSphereLUT) glDeleteTextures(1, &m_kerrPhotonSphereLUT);
    if (m_kerrISCOLUT) glDeleteTextures(1, &m_kerrISCOLUT);
    if (m_quadVAO) glDeleteVertexArrays(1, &m_quadVAO);
    if (m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
    if (m_meshDataSSBO) glDeleteBuffers(1, &m_meshDataSSBO);
    if (m_triangleSSBO) glDeleteBuffers(1, &m_triangleSSBO);
}

void BlackHoleRenderer::Init(int width, int height) {
    m_width = width;
    m_height = height;

    m_computeShader = std::make_unique<Shader>("../shaders/black_hole_rendering.comp", true);
    m_displayShader = std::make_unique<Shader>("../shaders/blackhole_display.vert", "../shaders/blackhole_display.frag");
    m_bloomExtractShader = std::make_unique<Shader>("../shaders/bloom_extract.comp", true);
    m_bloomBlurShader = std::make_unique<Shader>("../shaders/bloom_blur.comp", true);
    m_lensFlareShader = std::make_unique<Shader>("../shaders/lens_flare.comp", true);

    m_blackbodyLUTGenerator = std::make_unique<MoleHole::BlackbodyLUTGenerator>();
    m_accelerationLUTGenerator = std::make_unique<MoleHole::AccelerationLUTGenerator>();
    m_hrDiagramLUTGenerator = std::make_unique<MoleHole::HRDiagramLUTGenerator>();
    m_kerrGeodesicLUTGenerator = std::make_unique<MoleHole::KerrGeodesicLUTGenerator>();

    CreateComputeTexture();
    CreateBloomTextures();
    CreateFullscreenQuad();
    CreateMeshBuffers();
    LoadSkybox();
    GenerateBlackbodyLUT();
    GenerateAccelerationLUT();
    GenerateHRDiagramLUT();
    // GenerateKerrGeodesicLUTs();

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

void BlackHoleRenderer::GenerateKerrGeodesicLUTs() {
    using namespace MoleHole;

    spdlog::info("Generating Kerr geodesic LUTs...");

    // Generate deflection LUT (3D: spin x inclination x impact parameter)
    auto deflectionData = KerrGeodesicLUTGenerator::generateDeflectionLUT();

    if (m_kerrDeflectionLUT) {
        glDeleteTextures(1, &m_kerrDeflectionLUT);
    }

    glGenTextures(1, &m_kerrDeflectionLUT);
    glBindTexture(GL_TEXTURE_3D, m_kerrDeflectionLUT);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F,
                 KerrGeodesicLUTGenerator::LUT_IMPACT_PARAM_SAMPLES,
                 KerrGeodesicLUTGenerator::LUT_INCLINATION_SAMPLES,
                 KerrGeodesicLUTGenerator::LUT_SPIN_SAMPLES,
                 0, GL_RED, GL_FLOAT, deflectionData.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    spdlog::info("Kerr deflection LUT uploaded to GPU");

    // Generate redshift LUT (3D)
    auto redshiftData = KerrGeodesicLUTGenerator::generateRedshiftLUT();

    if (m_kerrRedshiftLUT) {
        glDeleteTextures(1, &m_kerrRedshiftLUT);
    }

    glGenTextures(1, &m_kerrRedshiftLUT);
    glBindTexture(GL_TEXTURE_3D, m_kerrRedshiftLUT);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F,
                 KerrGeodesicLUTGenerator::LUT_IMPACT_PARAM_SAMPLES,
                 KerrGeodesicLUTGenerator::LUT_INCLINATION_SAMPLES,
                 KerrGeodesicLUTGenerator::LUT_SPIN_SAMPLES,
                 0, GL_RED, GL_FLOAT, redshiftData.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    spdlog::info("Kerr redshift LUT uploaded to GPU");

    // Generate photon sphere LUT (2D)
    auto photonSphereData = KerrGeodesicLUTGenerator::generatePhotonSphereLUT();

    if (m_kerrPhotonSphereLUT) {
        glDeleteTextures(1, &m_kerrPhotonSphereLUT);
    }

    glGenTextures(1, &m_kerrPhotonSphereLUT);
    glBindTexture(GL_TEXTURE_2D, m_kerrPhotonSphereLUT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F,
                 KerrGeodesicLUTGenerator::LUT_INCLINATION_SAMPLES,
                 KerrGeodesicLUTGenerator::LUT_SPIN_SAMPLES,
                 0, GL_RED, GL_FLOAT, photonSphereData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    spdlog::info("Kerr photon sphere LUT uploaded to GPU");

    // Generate ISCO LUT (1D, stored as 2D texture with height=1)
    auto iscoData = KerrGeodesicLUTGenerator::generateISCOLUT();

    if (m_kerrISCOLUT) {
        glDeleteTextures(1, &m_kerrISCOLUT);
    }

    glGenTextures(1, &m_kerrISCOLUT);
    glBindTexture(GL_TEXTURE_2D, m_kerrISCOLUT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F,
                 KerrGeodesicLUTGenerator::LUT_SPIN_SAMPLES,
                 1,
                 0, GL_RED, GL_FLOAT, iscoData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    spdlog::info("Kerr ISCO LUT uploaded to GPU");
    spdlog::info("All Kerr geodesic LUTs generated successfully");
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
    if (m_lensFlareTexture) {
        glDeleteTextures(1, &m_lensFlareTexture);
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

    // Create lens flare texture
    glGenTextures(1, &m_lensFlareTexture);
    glBindTexture(GL_TEXTURE_2D, m_lensFlareTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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

void BlackHoleRenderer::Render(const std::vector<BlackHole>& blackHoles, const std::vector<Sphere>& spheres, const std::vector<MeshObject>& meshes, const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache, const Camera& camera, float time) {
    UpdateUniforms(blackHoles, spheres, camera, time);
    // UpdateMeshBuffers(meshes, meshCache);

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

    // Bind Kerr deflection LUT to unit 5
    if (m_kerrDeflectionLUT) {
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_3D, m_kerrDeflectionLUT);
        m_computeShader->SetInt("u_kerrDeflectionLUT", 5);
    }

    // Bind Kerr redshift LUT to unit 6
    if (m_kerrRedshiftLUT) {
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_3D, m_kerrRedshiftLUT);
        m_computeShader->SetInt("u_kerrRedshiftLUT", 6);
    }

    // Bind Kerr photon sphere LUT to unit 7
    if (m_kerrPhotonSphereLUT) {
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, m_kerrPhotonSphereLUT);
        m_computeShader->SetInt("u_kerrPhotonSphereLUT", 7);
    }

    // Bind Kerr ISCO LUT to unit 8
    if (m_kerrISCOLUT) {
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, m_kerrISCOLUT);
        m_computeShader->SetInt("u_kerrISCOLUT", 8);
    }

    // Enable Kerr physics if all LUTs are loaded AND user has enabled it
    bool kerrPhysicsEnabled = Application::Params().Get(Params::GRKerrPhysicsEnabled, true);
    bool useKerrPhysics = kerrPhysicsEnabled &&
                          (m_kerrDeflectionLUT && m_kerrRedshiftLUT &&
                           m_kerrPhotonSphereLUT && m_kerrISCOLUT);
    m_computeShader->SetInt("u_useKerrPhysics", useKerrPhysics ? 1 : 0);

    if (!blackHoles.empty()) {
        m_computeShader->SetInt("u_debugMode", Application::Params().Get(Params::RenderingDebugMode, 0));
    }

    unsigned int groupsX = (m_width + 15) / 16;
    unsigned int groupsY = (m_height + 15) / 16;
    m_computeShader->Dispatch(groupsX, groupsY, 1);

    // Ensure writes to the image are visible to subsequent texture fetches in the fragment shader
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    m_computeShader->Unbind();
    
    // Apply bloom effect
    ApplyBloom();

    // Apply lens flare effect (uses bloom output)
    // ApplyLensFlare();
}

void BlackHoleRenderer::ApplyBloom() {
    // Check if bloom is enabled
    bool bloomEnabled = Application::Params().Get(Params::RenderingBloomEnabled, true);
    if (!bloomEnabled) {
        return;
    }
    
    unsigned int groupsX = (m_width + 15) / 16;
    unsigned int groupsY = (m_height + 15) / 16;
    
    // Step 1: Extract bright areas
    m_bloomExtractShader->Bind();
    
    float bloomThreshold = Application::Params().Get(Params::RenderingBloomThreshold, 1.0f);
    m_bloomExtractShader->SetFloat("u_bloomThreshold", bloomThreshold);
    
    glBindImageTexture(0, m_computeTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, m_bloomBrightTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    
    m_bloomExtractShader->Unbind();
    
    // Step 2: Apply Gaussian blur (ping-pong between two textures)
    m_bloomBlurShader->Bind();
    
    int blurPasses = Application::Params().Get(Params::RenderingBloomBlurPasses, 5);

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

void BlackHoleRenderer::ApplyLensFlare() {
    // Check if lens flare is enabled
    bool lensFlareEnabled = Application::Params().Get(Params::RenderingLensFlareEnabled, true);
    if (!lensFlareEnabled || !m_lensFlareShader) {
        return;
    }

    unsigned int groupsX = (m_width + 15) / 16;
    unsigned int groupsY = (m_height + 15) / 16;

    m_lensFlareShader->Bind();

    float flareIntensity = Application::Params().Get(Params::RenderingLensFlareIntensity, 0.3f);
    float flareThreshold = Application::Params().Get(Params::RenderingLensFlareThreshold, 2.0f);

    m_lensFlareShader->SetFloat("u_flareIntensity", flareIntensity);
    m_lensFlareShader->SetFloat("u_flareThreshold", flareThreshold);
    m_lensFlareShader->SetInt("u_flareEnabled", 1);

    // Use bloom result as input (bright areas already extracted)
    glBindImageTexture(0, m_bloomBlurTexture[m_bloomFinalTextureIndex], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, m_lensFlareTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    m_lensFlareShader->Unbind();
}

void BlackHoleRenderer::UpdateUniforms(const std::vector<BlackHole>& blackHoles, const std::vector<Sphere>& spheres, const Camera& camera, float time) {
    m_computeShader->Bind();

    glm::vec3 cameraPos = camera.GetPosition();
    glm::vec3 cameraFront = camera.GetFront();
    glm::vec3 cameraUp = camera.GetUp();
    glm::vec3 cameraRight = glm::normalize(glm::cross(cameraFront, cameraUp));

    m_computeShader->SetVec3("u_cameraPos", cameraPos);
    m_computeShader->SetVec3("u_cameraFront", cameraFront);
    m_computeShader->SetVec3("u_cameraUp", cameraUp);
    m_computeShader->SetVec3("u_cameraRight", cameraRight);
    m_computeShader->SetFloat("u_fov", camera.GetFov());
    m_computeShader->SetInt("u_enableThirdPerson", Application::Params().Get(Params::RenderingThirdPerson, false) ? 1 : 0);
    m_computeShader->SetFloat("u_aspect", static_cast<float>(m_width) / static_cast<float>(m_height));
    m_computeShader->SetFloat("u_time", time);

    // Rendering settings from AppState
    m_computeShader->SetInt("u_gravitationalLensingEnabled", Application::Params().Get(Params::RenderingGravitationalLensingEnabled, true) ? 1 : 0);
    m_computeShader->SetInt("u_accretionDiskEnabled", Application::Params().Get(Params::RenderingAccretionDiskEnabled, true) ? 1 : 0);
    m_computeShader->SetInt("u_renderBlackHoles", Application::Params().Get(Params::RenderingBlackHolesEnabled, true) ? 1 : 0);
    m_computeShader->SetFloat("u_accDiskHeight", Application::Params().Get(Params::RenderingAccDiskHeight, 0.1f));
    m_computeShader->SetFloat("u_accDiskNoiseScale", Application::Params().Get(Params::RenderingAccDiskNoiseScale, 1.0f));
    m_computeShader->SetFloat("u_accDiskNoiseLOD", Application::Params().Get(Params::RenderingAccDiskNoiseLOD, 3.0f));
    m_computeShader->SetFloat("u_accDiskSpeed", Application::Params().Get(Params::RenderingAccDiskSpeed, 1.0f));
    m_computeShader->SetFloat("u_dopplerBeamingEnabled", Application::Params().Get(Params::RenderingDopplerBeamingEnabled, true) ? 1.0f : 0.0f);
    m_computeShader->SetFloat("u_accDiskTemp", 2000.0f);
    m_computeShader->SetInt("u_gravitationalRedshiftEnabled", Application::Params().Get(Params::RenderingGravitationalRedshiftEnabled, true) ? 1 : 0);

    // Ray marching quality settings - only set during export mode
    // In real-time mode, use shader defaults for better performance
    if (m_isExportMode) {
        m_computeShader->SetFloat("u_rayStepSize", Application::Params().Get(Params::RenderingRayStepSize, 0.1f));
        m_computeShader->SetInt("u_maxRaySteps", Application::Params().Get(Params::RenderingMaxRaySteps, 128));
        m_computeShader->SetFloat("u_adaptiveStepRate", Application::Params().Get(Params::RenderingAdaptiveStepRate, 0.1f));
    }

    // Black holes
    int numBlackHoles = std::min(static_cast<int>(blackHoles.size()), 8);
    m_computeShader->SetInt("u_numBlackHoles", numBlackHoles);

    for (int i = 0; i < numBlackHoles; i++) {
        const BlackHole& bh = blackHoles[i];

        // Convert mass to appropriate units for shader (using solar masses as base unit)
        float normalizedMass = bh.mass; // Assume mass is already in appropriate units, whatever these are like pls help

        std::string posUniform = "u_blackHolePositions[" + std::to_string(i) + "]";
        std::string massUniform = "u_blackHoleMasses[" + std::to_string(i) + "]";
        std::string spinUniform = "u_blackHoleSpins[" + std::to_string(i) + "]";
        std::string spinAxisUniform = "u_blackHoleSpinAxes[" + std::to_string(i) + "]";

        m_computeShader->SetVec3(posUniform, bh.position);
        m_computeShader->SetFloat(massUniform, normalizedMass);
        m_computeShader->SetFloat(spinUniform, bh.spin);
        m_computeShader->SetVec3(spinAxisUniform, glm::normalize(bh.spinAxis));
    }
    
    // Spheres
    int renderSpheres = 1; // Default enabled
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
    m_displayShader->Bind();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_computeTexture);
    m_displayShader->SetInt("u_raytracedImage", 0);

    // Bind bloom texture
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_bloomBlurTexture[m_bloomFinalTextureIndex]); // Use final blur result
    m_displayShader->SetInt("u_bloomImage", 1);
    
    // Bind lens flare texture
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_lensFlareTexture);
    m_displayShader->SetInt("u_lensFlareImage", 2);

    // Set bloom parameters
    bool bloomEnabled = Application::Params().Get(Params::RenderingBloomEnabled, true);
    float bloomIntensity = Application::Params().Get(Params::RenderingBloomIntensity, 5.0f);
    bool bloomDebug = Application::Params().Get(Params::RenderingBloomDebug, false);
    m_displayShader->SetInt("u_bloomEnabled", bloomEnabled ? 1 : 0);
    m_displayShader->SetFloat("u_bloomIntensity", bloomIntensity);
    m_displayShader->SetInt("u_bloomDebug", bloomDebug ? 1 : 0);

    // Set lens flare parameters
    bool lensFlareEnabled = Application::Params().Get(Params::RenderingLensFlareEnabled, true);
    float lensFlareIntensity = Application::Params().Get(Params::RenderingLensFlareIntensity, 1.0f);
    m_displayShader->SetInt("u_lensFlareEnabled", lensFlareEnabled ? 1 : 0);
    m_displayShader->SetFloat("u_lensFlareIntensity", lensFlareIntensity);

    // Set FXAA parameters
    bool fxaaEnabled = Application::Params().Get(Params::RenderingAntiAliasingEnabled, false);
    m_displayShader->SetInt("u_fxaaEnabled", fxaaEnabled ? 1 : 0);
    m_displayShader->SetFloat("rt_w", static_cast<float>(m_width));
    m_displayShader->SetFloat("rt_h", static_cast<float>(m_height));

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

void BlackHoleRenderer::CreateMeshBuffers() {
    glGenBuffers(1, &m_meshDataSSBO);
    glGenBuffers(1, &m_triangleSSBO);
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_meshDataSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_meshDataSSBO);
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_triangleSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_triangleSSBO);
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void BlackHoleRenderer::UpdateMeshBuffers(const std::vector<MeshObject>& meshes, const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache) {
    struct MeshData {
        glm::mat4 transform;
        glm::vec4 baseColor;
        float metallic;
        float roughness;
        int triangleCount;
        int triangleOffset;
    };
    
    struct Triangle {
        glm::vec3 v0, v1, v2;
        glm::vec3 n0, n1, n2;
    };
    
    std::vector<MeshData> meshDataArray;
    std::vector<Triangle> triangleArray;
    
    int triangleOffset = 0;
    constexpr int MAX_MESHES = 3;
    int totalTriangles = 0;
    
    for (size_t i = 0; i < meshes.size() && i < MAX_MESHES; ++i) {
        const auto& meshObj = meshes[i];
        
        auto it = meshCache.find(meshObj.path);
        if (it == meshCache.end() || !it->second || !it->second->IsLoaded()) {
            continue;
        }
        
        auto gltfMesh = it->second;
        auto geometry = gltfMesh->GetPhysicsGeometry();
        
        if (geometry.vertices.empty() || geometry.indices.empty()) {
            continue;
        }
        
        MeshData data{};
        data.transform = glm::translate(glm::mat4(1.0f), meshObj.position);
        data.transform = data.transform * glm::mat4_cast(meshObj.rotation);
        data.transform = glm::scale(data.transform, meshObj.scale);
        data.baseColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        data.metallic = 0.5f;
        data.roughness = 0.5f;
        data.triangleOffset = triangleOffset;
        
        int meshTriangleCount = 0;

        for (size_t idx = 0; idx < geometry.indices.size() && idx + 2 < geometry.indices.size(); idx += 3) {
            constexpr int MAX_TRIANGLES_PER_MESH = 50000;
            if (totalTriangles >= MAX_MESHES * MAX_TRIANGLES_PER_MESH) {
                spdlog::warn("Exceeded maximum triangle count, skipping remaining triangles");
                break;
            }
            
            if (meshTriangleCount >= MAX_TRIANGLES_PER_MESH) {
                spdlog::warn("Mesh {} exceeded MAX_TRIANGLES_PER_MESH, skipping remaining triangles", meshObj.path);
                break;
            }
            
            unsigned int i0 = geometry.indices[idx + 0];
            unsigned int i1 = geometry.indices[idx + 1];
            unsigned int i2 = geometry.indices[idx + 2];
            
            if (i0 >= geometry.vertices.size() || i1 >= geometry.vertices.size() || i2 >= geometry.vertices.size()) {
                continue;
            }
            
            Triangle tri{};
            tri.v0 = geometry.vertices[i0];
            tri.v1 = geometry.vertices[i1];
            tri.v2 = geometry.vertices[i2];
            
            glm::vec3 edge1 = tri.v1 - tri.v0;
            glm::vec3 edge2 = tri.v2 - tri.v0;
            glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));
            
            tri.n0 = normal;
            tri.n1 = normal;
            tri.n2 = normal;
            
            triangleArray.push_back(tri);
            meshTriangleCount++;
            totalTriangles++;
        }
        
        data.triangleCount = meshTriangleCount;
        triangleOffset += meshTriangleCount;
        
        meshDataArray.push_back(data);
    }

    if (!meshDataArray.empty()) {
        spdlog::info("Updated mesh buffers: {} meshes, {} triangles", meshDataArray.size(), triangleArray.size());
    }
    
    m_computeShader->Bind();
    m_computeShader->SetInt("u_numMeshes", static_cast<int>(meshDataArray.size()));
    m_computeShader->SetInt("u_renderMeshes", meshDataArray.empty() ? 0 : 1);
    m_computeShader->Unbind();

    if (!meshDataArray.empty()) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_meshDataSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, meshDataArray.size() * sizeof(MeshData), 
                     meshDataArray.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_meshDataSSBO);
    }
    
    if (!triangleArray.empty()) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_triangleSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, triangleArray.size() * sizeof(Triangle), 
                     triangleArray.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_triangleSSBO);
    }
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}
