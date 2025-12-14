#pragma once
#include <memory>
#include <vector>
#include <functional>
#include <string>

class Scene;
class Camera;

struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct SwsContext;

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
        bool useCustomRaySettings = false;
        float customRayStepSize = 0.01f;
        int customMaxRaySteps = 1000;
    };

    ExportRenderer();
    ~ExportRenderer();

    void StartImageExport(const ImageConfig& config, const std::string& outputPath, Scene* scene);
    void StartVideoExport(const VideoConfig& config, const std::string& outputPath, Scene* scene);

    void Update();

    bool IsExporting() const { return m_isExporting; }
    float GetProgress() const { return m_progress; }
    const std::string& GetCurrentTask() const { return m_currentTask; }

private:
    void InitializeOffscreenBuffers(int width, int height);
    void CleanupOffscreenBuffers();
    void RenderFrame(Scene* scene, int width, int height);
    void CaptureFramePixels(std::vector<unsigned char>& pixels, int width, int height);
    bool SaveImagePNG(const std::string& path, int width, int height, const std::vector<unsigned char>& pixels);

    void ProcessImageExport();
    void ProcessVideoExport();
    void FinishExport();

    enum class ExportType {
        None,
        Image,
        Video
    };

    bool m_isExporting = false;
    float m_progress = 0.0f;
    std::string m_currentTask;
    ExportType m_exportType = ExportType::None;

    unsigned int m_fbo = 0;
    unsigned int m_colorTexture = 0;
    unsigned int m_depthRenderbuffer = 0;

    std::unique_ptr<Camera> m_camera;

    ImageConfig m_imageConfig;
    VideoConfig m_videoConfig;
    std::string m_outputPath;
    Scene* m_scene = nullptr;

    int m_currentFrame = 0;
    int m_totalFrames = 0;

    AVCodecContext* m_codecContext = nullptr;
    AVFormatContext* m_formatContext = nullptr;
    AVFrame* m_avFrame = nullptr;
    SwsContext* m_swsContext = nullptr;

    std::vector<unsigned char> m_pixelBuffer;
    std::vector<unsigned char> m_rgbBuffer;
    
    // Store original settings for restoration after export
    float m_savedRayStepSize = 0.01f;
    int m_savedMaxRaySteps = 1000;
};

