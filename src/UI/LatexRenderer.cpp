#include "LatexRenderer.h"

#include <glad/gl.h>
#include <spdlog/spdlog.h>
#include <stb_image.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace fs = std::filesystem;

LatexRenderer& LatexRenderer::Instance() {
    static LatexRenderer instance;
    return instance;
}

LatexRenderer::~LatexRenderer() {
    Shutdown();
}

void LatexRenderer::Init() {
    if (m_initialized) return;

    m_tempDir = (fs::temp_directory_path() / "molehole_latex").string();
    fs::create_directories(m_tempDir);

    auto result = std::system("latex --version > /dev/null 2>&1");
    bool hasLatex = (result == 0);

    result = std::system("dvipng --version > /dev/null 2>&1");
    bool hasDvipng = (result == 0);

    m_latexAvailable = hasLatex && hasDvipng;

    if (m_latexAvailable) {
        spdlog::info("LatexRenderer: latex + dvipng found, rendering enabled");
    } else {
        spdlog::warn("LatexRenderer: latex or dvipng not found, rendering disabled");
    }

    m_initialized = true;
}

void LatexRenderer::Shutdown() {
    if (!m_initialized) return;

    for (auto& [key, tex] : m_cache) {
        if (tex.textureID) {
            glDeleteTextures(1, &tex.textureID);
        }
    }
    m_cache.clear();

    try {
        if (!m_tempDir.empty() && fs::exists(m_tempDir)) {
            fs::remove_all(m_tempDir);
        }
    } catch (...) {}

    m_initialized = false;
}

std::string LatexRenderer::HashLatex(const std::string& latex, int dpi) {
    std::size_t h = std::hash<std::string>{}(latex);
    h ^= std::hash<int>{}(dpi) + 0x9e3779b9 + (h << 6) + (h >> 2);
    std::ostringstream oss;
    oss << std::hex << h;
    return oss.str();
}

bool LatexRenderer::RenderToPNG(const std::string& latex, const std::string& pngPath, int dpi) {
    std::string hash = HashLatex(latex, dpi);
    std::string texPath = m_tempDir + "/" + hash + ".tex";
    std::string dviPath = m_tempDir + "/" + hash + ".dvi";

    {
        std::ofstream ofs(texPath);
        if (!ofs) {
            spdlog::error("LatexRenderer: failed to write tex file {}", texPath);
            return false;
        }
        ofs << "\\documentclass[preview,border=2pt]{standalone}\n"
            << "\\usepackage{amsmath}\n"
            << "\\usepackage{xcolor}\n"
            << "\\begin{document}\n"
            << "\\color{white}\n"
            << latex << "\n"
            << "\\end{document}\n";
    }

    std::string latexCmd = "cd " + m_tempDir +
                           " && latex -interaction=nonstopmode " + hash + ".tex"
                           " > /dev/null 2>&1";
    if (std::system(latexCmd.c_str()) != 0) {
        spdlog::error("LatexRenderer: latex compilation failed for: {}", latex);
        return false;
    }

    std::ostringstream dvipngCmd;
    dvipngCmd << "dvipng"
              << " -D " << dpi
              << " --truecolor"
              << " -bg Transparent"
              << " -fg 'rgb 1.0 1.0 1.0'"
              << " -o " << pngPath
              << " " << dviPath
              << " > /dev/null 2>&1";
    if (std::system(dvipngCmd.str().c_str()) != 0) {
        spdlog::error("LatexRenderer: dvipng conversion failed for: {}", latex);
        return false;
    }

    return fs::exists(pngPath);
}

LatexTexture LatexRenderer::LoadPNGToTexture(const std::string& pngPath) {
    LatexTexture result;

    int channels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(pngPath.c_str(), &result.width, &result.height, &channels, 4);
    if (!data) {
        spdlog::error("LatexRenderer: failed to load PNG {}", pngPath);
        return result;
    }

    glGenTextures(1, &result.textureID);
    glBindTexture(GL_TEXTURE_2D, result.textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, result.width, result.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    result.valid = true;
    spdlog::debug("LatexRenderer: loaded texture {}x{} from {}", result.width, result.height, pngPath);
    return result;
}

const LatexTexture& LatexRenderer::RenderLatex(const std::string& latex, int dpi) {
    if (!m_initialized) Init();

    if (!m_latexAvailable) return m_invalid;

    std::string key = HashLatex(latex, dpi);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    std::string pngPath = m_tempDir + "/" + key + ".png";

    if (!RenderToPNG(latex, pngPath, dpi)) {
        m_cache[key] = m_invalid;
        return m_cache[key];
    }

    m_cache[key] = LoadPNGToTexture(pngPath);
    return m_cache[key];
}

void LatexRenderer::Invalidate() {
    for (auto& [key, tex] : m_cache) {
        if (tex.textureID) {
            glDeleteTextures(1, &tex.textureID);
        }
    }
    m_cache.clear();
}

