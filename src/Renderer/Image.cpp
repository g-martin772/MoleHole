#include "Image.h"
#include <glad/gl.h>
#include <spdlog/spdlog.h>

#include "stb_image.h"
#include <Application/Profiler.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>

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

namespace {
struct HDRCacheHeader {
    char magic[6];     // "MHDR\0\0"
    uint32_t version;  // 1
    uint32_t width;
    uint32_t height;
    uint32_t components; // number of channels
    uint64_t srcSize;    // bytes of source file
    uint64_t srcMTimeNs; // last write time in ns since epoch
};

inline uint64_t FileSize(const std::filesystem::path& p) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(p, ec);
    return ec ? 0ULL : static_cast<uint64_t>(sz);
}

inline uint64_t FileMTimeNs(const std::filesystem::path& p) {
    std::error_code ec;
    auto tp = std::filesystem::last_write_time(p, ec);
    if (ec) return 0ULL;
    return static_cast<uint64_t>(tp.time_since_epoch().count());
}
}

Image* Image::LoadHDR(const std::string& filepath) {
    PROFILE_FUNCTION();
    stbi_set_flip_vertically_on_load(true);

    int width, height, nrComponents;
    float* data = nullptr;
    std::vector<float> cacheBuffer;
    std::filesystem::path srcPath(filepath);
    std::filesystem::path cachePath = srcPath;
    cachePath += ".mhdr";

    bool loadedFromCache = false;
    if (std::filesystem::exists(cachePath)) {
        std::ifstream in(cachePath, std::ios::binary);
        HDRCacheHeader hdr{};
        if (in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr))) {
            if (std::memcmp(hdr.magic, "MHDR\0\0", 6) == 0 && hdr.version == 1) {
                uint64_t curSize = FileSize(srcPath);
                uint64_t curMTime = FileMTimeNs(srcPath);
                if (hdr.srcSize == curSize && hdr.srcMTimeNs == curMTime) {
                    width = static_cast<int>(hdr.width);
                    height = static_cast<int>(hdr.height);
                    nrComponents = static_cast<int>(hdr.components);
                    size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(nrComponents);
                    cacheBuffer.resize(count);
                    if (in.read(reinterpret_cast<char*>(cacheBuffer.data()), count * sizeof(float))) {
                        data = cacheBuffer.data();
                        loadedFromCache = true;
                    }
                }
            }
        }
    }

    if (!loadedFromCache) {
        data = stbi_loadf(filepath.c_str(), &width, &height, &nrComponents, 0);
    }

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

    if (!loadedFromCache) {
        std::error_code ec;
        uint64_t srcSize = FileSize(srcPath);
        uint64_t srcMTime = FileMTimeNs(srcPath);
        HDRCacheHeader hdr{};
        std::memset(&hdr, 0, sizeof(hdr));
        std::memcpy(hdr.magic, "MHDR\0\0", 6);
        hdr.version = 1;
        hdr.width = static_cast<uint32_t>(width);
        hdr.height = static_cast<uint32_t>(height);
        hdr.components = static_cast<uint32_t>(nrComponents);
        hdr.srcSize = srcSize;
        hdr.srcMTimeNs = srcMTime;

        std::ofstream out(cachePath, std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
            size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(nrComponents);
            out.write(reinterpret_cast<const char*>(data), count * sizeof(float));
        }
    }

    if (!loadedFromCache) {
        stbi_image_free(data);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    if (loadedFromCache) {
        spdlog::info("Loaded HDR texture from cache: {} ({}x{})", filepath, width, height);
    } else {
        spdlog::info("Successfully loaded HDR texture: {} ({}x{})", filepath, width, height);
    }

    return new Image(textureID, width, height);
}
