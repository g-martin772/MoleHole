//
// Created by leo on 10/7/25.
//

#include "VisualRenderer.h"
#include "stb_image.h"
#include <glad/gl.h>

#include <iostream>



VisualRenderer::VisualRenderer()
    : m_quadVAO(0), m_quadVBO(0), m_width(800), m_height(600) { }

VisualRenderer::~VisualRenderer() {
    if (m_quadVAO) glDeleteVertexArrays(1, &m_quadVAO);
    if (m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
}

void VisualRenderer::Init(int width, int height) {
    m_width = width;
    m_height = height;

    m_displayShader = std::make_unique<Shader>(
      "../shaders/black_hole_visual_display.vert",
      "../shaders/black_hole_visual_display.frag"
    );

    galaxy = loadCubemap("../assets/skybox_nebula_dark");
    colorMap = loadTexture2D("../assets/color_map.png");

    CreateFullscreenQuad();
}

void VisualRenderer::Render(const Camera &camera, float time) {

    m_displayShader->Bind();

    m_displayShader->SetVec2("resolution", glm::vec2(m_width, m_height));
    m_displayShader->SetInt("galaxy", galaxy);
    m_displayShader->SetInt("colorMap", colorMap);
    m_displayShader->SetFloat("time", time);
    m_displayShader->SetFloat("mouseX", 0.0f);
    m_displayShader->SetFloat("mouseY", 0.0f);

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    m_displayShader->Unbind();
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