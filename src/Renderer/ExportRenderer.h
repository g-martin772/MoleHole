#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>

class Scene;
class Camera;
class BlackHoleRenderer;
class VisualRenderer;

class ExportRenderer {
public:
    struct ImageConfig {
        int width = 1920;
        int height = 1080;
    };

    struct VideoConfig {
        int width = 1920;
        int height = 1080;
        float length = 10.0f;
        int framerate = 60;
        float tickrate = 60.0f;
    };

    ExportRenderer();
    ~ExportRenderer();

    void ExportImage(const ImageConfig& config, const std::string& outputPath, Scene* scene, 
                     std::function<void(float)> progressCallback = nullptr);
    void ExportVideo(const VideoConfig& config, const std::string& outputPath, Scene* scene,
                     std::function<void(float)> progressCallback = nullptr);

    bool IsExporting() const { return m_isExporting; }
    float GetProgress() const { return m_progress; }
    const std::string& GetCurrentTask() const { return m_currentTask; }

private:
    void InitializeOffscreenBuffers(int width, int height);
    void CleanupOffscreenBuffers();
    void RenderFrame(Scene* scene, int width, int height);
    void CaptureFramePixels(std::vector<unsigned char>& pixels, int width, int height);
    bool SaveImagePNG(const std::string& path, int width, int height, const std::vector<unsigned char>& pixels);

    bool m_isExporting = false;
    float m_progress = 0.0f;
    std::string m_currentTask;

    unsigned int m_fbo = 0;
    unsigned int m_colorTexture = 0;
    unsigned int m_depthRenderbuffer = 0;

    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<BlackHoleRenderer> m_blackHoleRenderer;
    std::unique_ptr<VisualRenderer> m_visualRenderer;
};
