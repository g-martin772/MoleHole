//
// Created by leo on 10/7/25.
//

#include "VisualRenderer.h"
#include "stb_image.h"
#include <glad/gl.h>

#include <iostream>



VisualRenderer::VisualRenderer()
    : m_texture(0), m_quadVAO(0), m_quadVBO(0), m_width(800), m_height(600) { }

VisualRenderer::~VisualRenderer() {
    if (m_texture) glDeleteTextures(1, &m_texture);
    if (m_quadVAO) glDeleteVertexArrays(1, &m_quadVAO);
    if (m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
}

void VisualRenderer::Init(int width, int height) {
    m_width = width;
    m_height = height;

    m_displayShader = std::make_unique<Shader>(
        "../shaders/blackhole_display.vert",
        "../shaders/blackhole_display.frag"
    );

    m_computeShader = std::make_unique<Shader>(
        "../shaders/black_hole_rendering.comp",
        true
    );

    galaxy = loadCubemap("../assets/skybox_nebula_dark");
    colorMap = loadTexture2D("../assets/color_map.png");
    
    CreateComputeTexture();
    CreateFullscreenQuad();
}

void VisualRenderer::Resize(int width, int height) {
    m_width = width;
    m_height = height;
    CreateComputeTexture();
}

void VisualRenderer::Render(std::vector<BlackHole> &black_holes, const Camera &camera, float time) {
    
    SetUniforms(black_holes, camera, time);
  
    m_computeShader->Bind();

    glBindImageTexture(0, m_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    if (galaxy) {
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_CUBE_MAP, galaxy);
      m_computeShader->SetInt("u_skyboxTexture", 1);
    }

    if (colorMap) {
      glActiveTexture(GL_TEXTURE2); 
      glBindTexture(GL_TEXTURE_2D, colorMap);
      m_computeShader->SetInt("u_colorMap", 2);
    }

  
    unsigned int groupsX = (m_width + 15) / 16;
    unsigned int groupsY = (m_height + 15) / 16;
    m_computeShader->Dispatch(groupsX, groupsY, 1);

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
  
    m_computeShader->Unbind();
  
    m_displayShader->Bind();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    m_displayShader->SetInt("u_raytracedImage", 0);
  
    /*
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, galaxy);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, colorMap);

    // Basic uniforms
    m_displayShader->SetVec2("resolution", glm::vec2(m_width, m_height));
    m_displayShader->SetInt("galaxy", 0);
    m_displayShader->SetInt("colorMap", 1);
    m_displayShader->SetFloat("time", time);
    
    // Camera uniforms - replacing hardcoded camera logic
    glm::vec3 cameraPos = camera.GetPosition();
    glm::vec3 cameraFront = camera.GetFront();
    glm::vec3 cameraUp = camera.GetUp();
    glm::vec3 cameraRight = glm::normalize(glm::cross(cameraFront, cameraUp));
    
    m_displayShader->SetVec3("cameraPos", cameraPos);
    m_displayShader->SetVec3("cameraFront", cameraFront);
    m_displayShader->SetVec3("cameraUp", cameraUp);
    m_displayShader->SetVec3("cameraRight", cameraRight);
    m_displayShader->SetFloat("cameraFov", camera.GetFov());
    m_displayShader->SetFloat("cameraAspect", (float)m_width / (float)m_height);
    
    // Black hole rendering control uniforms
    m_displayShader->SetFloat("gravatationalLensing", 1.0f);
    m_displayShader->SetFloat("renderBlackHole", 1.0f);
    m_displayShader->SetFloat("mouseControl", 0.0f);
    m_displayShader->SetFloat("fovScale", 1.0f);
    
    // Accretion disk uniforms
    m_displayShader->SetFloat("adiskEnabled", 1.0f);
    m_displayShader->SetFloat("adiskParticle", 1.0f);
    m_displayShader->SetFloat("adiskHeight", 0.2f);
    m_displayShader->SetFloat("adiskLit", 0.5f);
    m_displayShader->SetFloat("adiskDensityV", 1.0f);
    m_displayShader->SetFloat("adiskDensityH", 1.0f);
    m_displayShader->SetFloat("adiskNoiseScale", 1.0f);
    m_displayShader->SetFloat("adiskNoiseLOD", 5.0f);
    m_displayShader->SetFloat("adiskSpeed", 0.5f);
    
    // Legacy mouse controls (can be removed if not needed)
    m_displayShader->SetFloat("mouseX", 0.0f);
    m_displayShader->SetFloat("mouseY", 0.0f);

    */

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    m_displayShader->Unbind();
}

void VisualRenderer::CreateComputeTexture() {
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
    }

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindImageTexture(0, m_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
}

void VisualRenderer::CreateFullscreenQuad() {
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

GLuint VisualRenderer::loadTexture2D(const std::string &file, bool repeat) {
  GLuint textureID;
  glGenTextures(1, &textureID);

  int width, height, comp;
  unsigned char *data = stbi_load(file.c_str(), &width, &height, &comp, 0);
  if (data) {
    GLenum format;
    GLenum internalFormat;
    if (comp == 1) {
      format = GL_RED;
      internalFormat = GL_RED;
    } else if (comp == 3) {
      format = GL_RGB;
      internalFormat = GL_SRGB;
    } else if (comp == 4) {
      format = GL_RGBA;
      internalFormat = GL_SRGB_ALPHA;
    }

    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format,
                 GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                    repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                    repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
  } else {
    std::cout << "ERROR: Failed to load texture at: " << file << std::endl;
    stbi_image_free(data);
  }

  return textureID;
}

GLuint VisualRenderer::loadCubemap(const std::string &cubemapDir) {
  const std::vector<std::string> faces = {"right",  "left",  "top",
                                          "bottom", "front", "back"};

  GLuint textureID;
  glGenTextures(1, &textureID);
  glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

  int width, height, comp;
  for (GLuint i = 0; i < faces.size(); i++) {
    unsigned char *data =
        stbi_load((cubemapDir + "/" + faces[i] + ".png").c_str(), &width,
                  &height, &comp, 0);
    if (data) {
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_SRGB, width,
                   height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
      stbi_image_free(data);
    } else {
      std::cout << "Cubemap texture failed to load at path: "
                << (cubemapDir + "/" + faces[i] + ".png").c_str() << std::endl;
      stbi_image_free(data);
    }
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

  return textureID;
}

void VisualRenderer::SetUniforms(std::vector<BlackHole>& blackHoles, const Camera& camera, float time) {
    m_computeShader->Bind();

    int numBlackHoles = std::min(static_cast<int>(blackHoles.size()), 8);
    m_computeShader->SetInt("u_numBlackHoles", numBlackHoles);

    for (int i = 0; i < numBlackHoles; i++) {
        m_computeShader->SetVec3("u_blackHolePositions[" + std::to_string(i) + "]", blackHoles[i].position);
        m_computeShader->SetFloat("u_blackHoleMasses[" + std::to_string(i) + "]", blackHoles[i].mass);
    }

    m_computeShader->SetFloat("u_time", time);
    m_computeShader->SetVec3("u_cameraPosition", camera.GetPosition());
    m_computeShader->SetVec3("u_cameraFront", camera.GetFront());
    m_computeShader->SetVec3("u_cameraUp", camera.GetUp());
    m_computeShader->SetVec3("u_cameraRight", glm::normalize(glm::cross(camera.GetFront(), camera.GetUp())));
    m_computeShader->SetFloat("u_fov", camera.GetFov());
    m_computeShader->SetFloat("u_aspect", static_cast<float>(m_width) / static_cast<float>(m_height));
  
    m_computeShader->Unbind();
}