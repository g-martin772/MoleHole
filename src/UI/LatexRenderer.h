#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

struct LatexTexture {
    uint32_t textureID = 0;
    int width = 0;
    int height = 0;
    bool valid = false;
};

class LatexRenderer {
public:
    static LatexRenderer& Instance();

    void Init();
    void Shutdown();

    const LatexTexture& RenderLatex(const std::string& latex, int dpi = 200);
    void Invalidate();

private:
    LatexRenderer() = default;
    ~LatexRenderer();

    LatexRenderer(const LatexRenderer&) = delete;
    LatexRenderer& operator=(const LatexRenderer&) = delete;

    bool RenderToPNG(const std::string& latex, const std::string& pngPath, int dpi);
    LatexTexture LoadPNGToTexture(const std::string& pngPath);
    std::string HashLatex(const std::string& latex, int dpi);

    std::unordered_map<std::string, LatexTexture> m_cache;
    std::string m_tempDir;
    bool m_initialized = false;
    bool m_latexAvailable = false;
    LatexTexture m_invalid;
};

