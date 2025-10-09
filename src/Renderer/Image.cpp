#include "Image.h"
#include <glad/gl.h>
#include <spdlog/spdlog.h>

#include "stb_image.h"

Image::Image(int width, int height) : width(width), height(height), textureID(0) {
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

Image::Image(unsigned int textureID, int width, int height)
    : textureID(textureID), width(width), height(height) {
}

Image::~Image() {
    glDeleteTextures(1, &textureID);
}

Image* Image::LoadHDR(const std::string& filepath) {
    stbi_set_flip_vertically_on_load(true);

    int width, height, nrComponents;
    float* data = stbi_loadf(filepath.c_str(), &width, &height, &nrComponents, 0);

    if (!data) {
        spdlog::error("Failed to load HDR texture: {}", filepath);
        return nullptr;
    }

    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    GLenum format;
    if (nrComponents == 1)
        format = GL_RED;
    else if (nrComponents == 3)
        format = GL_RGB;
    else if (nrComponents == 4)
        format = GL_RGBA;
    else {
        spdlog::error("Unsupported HDR format with {} components", nrComponents);
        stbi_image_free(data);
        glDeleteTextures(1, &textureID);
        return nullptr;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, format, GL_FLOAT, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);

    spdlog::info("Successfully loaded HDR texture: {} ({}x{})", filepath, width, height);

    return new Image(textureID, width, height);
}
