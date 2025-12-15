#include "ExportRenderer.h"
#include <glad/gl.h>
#include "Simulation/Scene.h"
#include "Camera.h"
#include "Application/Application.h"
#include <spdlog/spdlog.h>
#include <stb_image_write.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

ExportRenderer::ExportRenderer() {
}

ExportRenderer::~ExportRenderer() {
    CleanupOffscreenBuffers();
}

void ExportRenderer::InitializeOffscreenBuffers(int width, int height) {
    CleanupOffscreenBuffers();

    spdlog::info("Initializing offscreen buffers: {}x{}", width, height);

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_colorTexture);
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        spdlog::error("OpenGL error after creating color texture: {}", error);
    }

    glGenRenderbuffers(1, &m_depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthRenderbuffer);

    error = glGetError();
    if (error != GL_NO_ERROR) {
        spdlog::error("OpenGL error after creating depth buffer: {}", error);
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        spdlog::error("Framebuffer is not complete! Status: 0x{:x}", status);
        switch (status) {
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
                spdlog::error("  - Incomplete attachment");
                break;
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
                spdlog::error("  - Missing attachment");
                break;
            case GL_FRAMEBUFFER_UNSUPPORTED:
                spdlog::error("  - Unsupported framebuffer format");
                break;
            default:
                spdlog::error("  - Unknown error");
                break;
        }
    } else {
        spdlog::info("Framebuffer initialized successfully");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ExportRenderer::CleanupOffscreenBuffers() {
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    if (m_colorTexture) {
        glDeleteTextures(1, &m_colorTexture);
        m_colorTexture = 0;
    }
    if (m_depthRenderbuffer) {
        glDeleteRenderbuffers(1, &m_depthRenderbuffer);
        m_depthRenderbuffer = 0;
    }
}

void ExportRenderer::RenderFrame(Scene* scene, int width, int height) {
    auto& renderer = Application::GetRenderer();
    renderer.RenderToFramebuffer(m_fbo, width, height, scene, m_camera.get());
}

void ExportRenderer::CaptureFramePixels(std::vector<unsigned char>& pixels, int width, int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glFlush();

    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        spdlog::error("OpenGL error during pixel capture: {}", error);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool ExportRenderer::SaveImagePNG(const std::string& path, int width, int height, const std::vector<unsigned char>& pixels) {
    std::vector<unsigned char> flippedPixels = pixels;
    
    int rowSize = width * 3;
    std::vector<unsigned char> temp(rowSize);
    for (int y = 0; y < height / 2; ++y) {
        int topRow = y * rowSize;
        int bottomRow = (height - 1 - y) * rowSize;
        std::copy(flippedPixels.begin() + topRow, flippedPixels.begin() + topRow + rowSize, temp.begin());
        std::copy(flippedPixels.begin() + bottomRow, flippedPixels.begin() + bottomRow + rowSize, flippedPixels.begin() + topRow);
        std::copy(temp.begin(), temp.end(), flippedPixels.begin() + bottomRow);
    }

    int result = stbi_write_png(path.c_str(), width, height, 3, flippedPixels.data(), width * 3);
    return result != 0;
}

void ExportRenderer::StartImageExport(const ImageConfig& config, const std::string& outputPath, Scene* scene) {
    if (m_isExporting) {
        spdlog::warn("Export already in progress, ignoring new request");
        return;
    }

    m_isExporting = true;
    m_exportType = ExportType::Image;
    m_progress = 0.0f;
    m_currentTask = "Starting image export...";
    m_imageConfig = config;
    m_outputPath = outputPath;
    m_scene = scene;
    m_currentFrame = 0;

    spdlog::info("Starting image export: {}x{} to {}", config.width, config.height, outputPath);
}

void ExportRenderer::StartVideoExport(const VideoConfig& config, const std::string& outputPath, Scene* scene) {
    if (m_isExporting) {
        spdlog::warn("Export already in progress, ignoring new request");
        return;
    }

    m_isExporting = true;
    m_exportType = ExportType::Video;
    m_progress = 0.0f;
    m_currentTask = "Starting video export...";
    m_videoConfig = config;
    m_outputPath = outputPath;
    m_scene = scene;
    m_currentFrame = 0;
    m_totalFrames = static_cast<int>(config.length * config.framerate);

    // Enable export mode on renderer if custom ray settings are used
    auto& renderer = Application::GetRenderer();
    if (config.useCustomRaySettings) {
        renderer.blackHoleRenderer->SetExportMode(true);
    }

    spdlog::info("Starting video export: {}x{} {} frames to {}", config.width, config.height, m_totalFrames, outputPath);
}

void ExportRenderer::Update() {
    if (!m_isExporting) {
        return;
    }

    try {
        if (m_exportType == ExportType::Image) {
            ProcessImageExport();
        } else if (m_exportType == ExportType::Video) {
            constexpr int framesPerUpdate = 5;
            for (int i = 0; i < framesPerUpdate && m_isExporting; ++i) {
                ProcessVideoExport();
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Export failed: {}", e.what());
        m_currentTask = "Failed: " + std::string(e.what());
        FinishExport();
    }
}

void ExportRenderer::ProcessImageExport() {
    auto& renderer = Application::GetRenderer();

    switch (m_currentFrame) {
        case 0:
            m_currentTask = "Initializing camera...";
            m_progress = 0.1f;

            m_camera = std::make_unique<Camera>(
                Application::State().rendering.fov,
                (float)m_imageConfig.width / (float)m_imageConfig.height,
                0.01f, 10000.0f
            );
            m_camera->SetPosition(renderer.camera->GetPosition());
            m_camera->SetYawPitch(renderer.camera->GetYaw(), renderer.camera->GetPitch());

            m_pixelBuffer.resize(m_imageConfig.width * m_imageConfig.height * 4);
            m_rgbBuffer.resize(m_imageConfig.width * m_imageConfig.height * 3);

            m_currentFrame++;
            break;

        case 1:
            m_currentTask = "Setting up framebuffer...";
            m_progress = 0.2f;
            InitializeOffscreenBuffers(m_imageConfig.width, m_imageConfig.height);
            m_currentFrame++;
            break;

        case 2:
            m_currentTask = "Rendering frame...";
            m_progress = 0.5f;
            RenderFrame(m_scene, m_imageConfig.width, m_imageConfig.height);
            m_currentFrame++;
            break;

        case 3:
            m_currentTask = "Capturing pixels...";
            m_progress = 0.7f;
            CaptureFramePixels(m_pixelBuffer, m_imageConfig.width, m_imageConfig.height);

            for (int i = 0; i < m_imageConfig.width * m_imageConfig.height; ++i) {
                m_rgbBuffer[i * 3 + 0] = m_pixelBuffer[i * 4 + 0];
                m_rgbBuffer[i * 3 + 1] = m_pixelBuffer[i * 4 + 1];
                m_rgbBuffer[i * 3 + 2] = m_pixelBuffer[i * 4 + 2];
            }

            m_currentFrame++;
            break;

        case 4:
            m_currentTask = "Saving PNG...";
            m_progress = 0.9f;

            if (SaveImagePNG(m_outputPath, m_imageConfig.width, m_imageConfig.height, m_rgbBuffer)) {
                spdlog::info("Image exported successfully to: {}", m_outputPath);
                m_currentTask = "Complete";
            } else {
                spdlog::error("Failed to save image to: {}", m_outputPath);
                m_currentTask = "Failed to save image";
            }

            m_progress = 1.0f;
            FinishExport();
            break;
    }
}

void ExportRenderer::ProcessVideoExport() {
    auto& renderer = Application::GetRenderer();
    auto& simulation = Application::GetSimulation();

    if (m_currentFrame == 0) {
        m_currentTask = "Initializing video encoder...";
        m_progress = 0.0f;

        m_camera = std::make_unique<Camera>(
            Application::State().rendering.fov,
            (float)m_videoConfig.width / (float)m_videoConfig.height,
            0.01f, 10000.0f
        );
        m_camera->SetPosition(renderer.camera->GetPosition());
        m_camera->SetYawPitch(renderer.camera->GetYaw(), renderer.camera->GetPitch());

        InitializeOffscreenBuffers(m_videoConfig.width, m_videoConfig.height);

        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) {
            throw std::runtime_error("H264 codec not found");
        }

        m_codecContext = avcodec_alloc_context3(codec);
        m_codecContext->bit_rate = 4000000;
        m_codecContext->width = m_videoConfig.width;
        m_codecContext->height = m_videoConfig.height;
        m_codecContext->time_base = AVRational{1, m_videoConfig.framerate};
        m_codecContext->framerate = AVRational{m_videoConfig.framerate, 1};
        m_codecContext->gop_size = 10;
        m_codecContext->max_b_frames = 1;
        m_codecContext->pix_fmt = AV_PIX_FMT_YUV420P;

        if (avcodec_open2(m_codecContext, codec, nullptr) < 0) {
            throw std::runtime_error("Could not open codec");
        }

        avformat_alloc_output_context2(&m_formatContext, nullptr, nullptr, m_outputPath.c_str());
        if (!m_formatContext) {
            throw std::runtime_error("Could not create output context");
        }

        AVStream* stream = avformat_new_stream(m_formatContext, nullptr);
        stream->time_base = m_codecContext->time_base;
        avcodec_parameters_from_context(stream->codecpar, m_codecContext);

        if (!(m_formatContext->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&m_formatContext->pb, m_outputPath.c_str(), AVIO_FLAG_WRITE) < 0) {
                throw std::runtime_error("Could not open output file");
            }
        }

        if (avformat_write_header(m_formatContext, nullptr) < 0) {
            throw std::runtime_error("Could not write format header");
        }

        m_avFrame = av_frame_alloc();
        m_avFrame->format = m_codecContext->pix_fmt;
        m_avFrame->width = m_codecContext->width;
        m_avFrame->height = m_codecContext->height;
        av_frame_get_buffer(m_avFrame, 0);

        m_swsContext = sws_getContext(
            m_videoConfig.width, m_videoConfig.height, AV_PIX_FMT_RGB24,
            m_videoConfig.width, m_videoConfig.height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        m_pixelBuffer.resize(m_videoConfig.width * m_videoConfig.height * 4);
        m_rgbBuffer.resize(m_videoConfig.width * m_videoConfig.height * 3);

        // Store original ray marching settings and apply custom settings if requested
        if (m_videoConfig.useCustomRaySettings) {
            m_savedRayStepSize = Application::State().rendering.rayStepSize;
            m_savedMaxRaySteps = Application::State().rendering.maxRaySteps;
            
            Application::State().rendering.rayStepSize = m_videoConfig.customRayStepSize;
            Application::State().rendering.maxRaySteps = m_videoConfig.customMaxRaySteps;
            
            spdlog::info("Using custom ray marching settings for export: step size = {}, max steps = {}", 
                        m_videoConfig.customRayStepSize, m_videoConfig.customMaxRaySteps);
        }

        simulation.Stop();
        simulation.Start();

        m_currentFrame = 1;
        spdlog::info("Video encoder initialized, starting frame rendering");
    } else if (m_currentFrame <= m_totalFrames) {
        m_currentTask = "Rendering frame " + std::to_string(m_currentFrame) + "/" + std::to_string(m_totalFrames);
        m_progress = static_cast<float>(m_currentFrame - 1) / m_totalFrames;

        // Fixed: use framerate instead of tickrate to ensure proper video playback speed
        // Each frame should advance simulation by 1/framerate seconds
        float simulationTimePerFrame = 1.0f / m_videoConfig.framerate;
        simulation.Update(simulationTimePerFrame);

        RenderFrame(m_scene, m_videoConfig.width, m_videoConfig.height);
        CaptureFramePixels(m_pixelBuffer, m_videoConfig.width, m_videoConfig.height);

        for (int i = 0; i < m_videoConfig.width * m_videoConfig.height; ++i) {
            m_rgbBuffer[i * 3 + 0] = m_pixelBuffer[i * 4 + 0];
            m_rgbBuffer[i * 3 + 1] = m_pixelBuffer[i * 4 + 1];
            m_rgbBuffer[i * 3 + 2] = m_pixelBuffer[i * 4 + 2];
        }

        std::vector<unsigned char> flippedPixels = m_rgbBuffer;
        int rowSize = m_videoConfig.width * 3;
        std::vector<unsigned char> temp(rowSize);
        for (int y = 0; y < m_videoConfig.height / 2; ++y) {
            int topRow = y * rowSize;
            int bottomRow = (m_videoConfig.height - 1 - y) * rowSize;
            std::copy(flippedPixels.begin() + topRow, flippedPixels.begin() + topRow + rowSize, temp.begin());
            std::copy(flippedPixels.begin() + bottomRow, flippedPixels.begin() + bottomRow + rowSize, flippedPixels.begin() + topRow);
            std::copy(temp.begin(), temp.end(), flippedPixels.begin() + bottomRow);
        }

        const uint8_t* srcData[1] = { flippedPixels.data() };
        int srcLinesize[1] = { m_videoConfig.width * 3 };
        sws_scale(m_swsContext, srcData, srcLinesize, 0, m_videoConfig.height, m_avFrame->data, m_avFrame->linesize);

        m_avFrame->pts = m_currentFrame - 1;

        AVPacket* pkt = av_packet_alloc();
        int ret = avcodec_send_frame(m_codecContext, m_avFrame);
        while (ret >= 0) {
            ret = avcodec_receive_packet(m_codecContext, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                spdlog::error("Error encoding frame");
                break;
            }
            av_packet_rescale_ts(pkt, m_codecContext->time_base, m_formatContext->streams[0]->time_base);
            pkt->stream_index = 0;
            av_interleaved_write_frame(m_formatContext, pkt);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);

        m_currentFrame++;
    } else {
        m_currentTask = "Finalizing video...";
        m_progress = 0.95f;

        avcodec_send_frame(m_codecContext, nullptr);
        AVPacket* pkt = av_packet_alloc();
        int ret;
        while ((ret = avcodec_receive_packet(m_codecContext, pkt)) >= 0) {
            av_packet_rescale_ts(pkt, m_codecContext->time_base, m_formatContext->streams[0]->time_base);
            pkt->stream_index = 0;
            av_interleaved_write_frame(m_formatContext, pkt);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);

        av_write_trailer(m_formatContext);

        sws_freeContext(m_swsContext);
        av_frame_free(&m_avFrame);

        if (!(m_formatContext->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&m_formatContext->pb);
        }
        
        avformat_free_context(m_formatContext);
        avcodec_free_context(&m_codecContext);

        m_swsContext = nullptr;
        m_avFrame = nullptr;
        m_formatContext = nullptr;
        m_codecContext = nullptr;

        // Restore original ray marching settings if custom settings were used
        if (m_videoConfig.useCustomRaySettings) {
            Application::State().rendering.rayStepSize = m_savedRayStepSize;
            Application::State().rendering.maxRaySteps = m_savedMaxRaySteps;
            spdlog::info("Restored original ray marching settings");
        }

        m_currentTask = "Complete";
        m_progress = 1.0f;

        spdlog::info("Video exported successfully to: {}", m_outputPath);
        FinishExport();
    }
}

void ExportRenderer::FinishExport() {
    // Disable export mode on renderer
    auto& renderer = Application::GetRenderer();
    renderer.blackHoleRenderer->SetExportMode(false);
    
    m_camera.reset();
    CleanupOffscreenBuffers();
    m_pixelBuffer.clear();
    m_rgbBuffer.clear();
    m_isExporting = false;
    m_exportType = ExportType::None;
    m_scene = nullptr;
    m_currentFrame = 0;
}
