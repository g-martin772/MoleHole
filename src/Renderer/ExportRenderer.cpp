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

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_colorTexture);
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);

    glGenRenderbuffers(1, &m_depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthRenderbuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        spdlog::error("Framebuffer is not complete!");
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
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
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

void ExportRenderer::ExportImage(const ImageConfig& config, const std::string& outputPath, Scene* scene,
                                 std::function<void(float)> progressCallback) {
    m_isExporting = true;
    m_progress = 0.0f;
    m_currentTask = "Initializing image export...";
    
    if (progressCallback) progressCallback(0.0f);

    try {
        auto& renderer = Application::GetRenderer();
        
        m_camera = std::make_unique<Camera>(
            Application::State().rendering.fov,
            (float)config.width / (float)config.height,
            0.01f, 10000.0f
        );
        m_camera->SetPosition(renderer.camera->GetPosition());
        m_camera->SetYawPitch(renderer.camera->GetYaw(), renderer.camera->GetPitch());

        m_currentTask = "Setting up framebuffer...";
        m_progress = 0.2f;
        if (progressCallback) progressCallback(0.2f);

        InitializeOffscreenBuffers(config.width, config.height);

        m_currentTask = "Rendering frame...";
        m_progress = 0.5f;
        if (progressCallback) progressCallback(0.5f);

        RenderFrame(scene, config.width, config.height);

        m_currentTask = "Capturing pixels...";
        m_progress = 0.7f;
        if (progressCallback) progressCallback(0.7f);

        std::vector<unsigned char> pixels(config.width * config.height * 3);
        CaptureFramePixels(pixels, config.width, config.height);

        m_currentTask = "Saving PNG...";
        m_progress = 0.9f;
        if (progressCallback) progressCallback(0.9f);

        if (SaveImagePNG(outputPath, config.width, config.height, pixels)) {
            spdlog::info("Image exported successfully to: {}", outputPath);
        } else {
            spdlog::error("Failed to save image to: {}", outputPath);
        }

        m_currentTask = "Complete";
        m_progress = 1.0f;
        if (progressCallback) progressCallback(1.0f);

    } catch (const std::exception& e) {
        spdlog::error("Image export failed: {}", e.what());
        m_currentTask = "Failed";
    }

    m_camera.reset();
    CleanupOffscreenBuffers();
    
    m_isExporting = false;
}

void ExportRenderer::ExportVideo(const VideoConfig& config, const std::string& outputPath, Scene* scene,
                                 std::function<void(float)> progressCallback) {
    m_isExporting = true;
    m_progress = 0.0f;
    m_currentTask = "Initializing video export...";
    
    if (progressCallback) progressCallback(0.0f);

    try {
        auto& renderer = Application::GetRenderer();
        
        m_camera = std::make_unique<Camera>(
            Application::State().rendering.fov,
            (float)config.width / (float)config.height,
            0.01f, 10000.0f
        );
        m_camera->SetPosition(renderer.camera->GetPosition());
        m_camera->SetYawPitch(renderer.camera->GetYaw(), renderer.camera->GetPitch());

        InitializeOffscreenBuffers(config.width, config.height);

        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) {
            spdlog::error("H264 codec not found");
            m_isExporting = false;
            return;
        }

        AVCodecContext* codecContext = avcodec_alloc_context3(codec);
        codecContext->bit_rate = 4000000;
        codecContext->width = config.width;
        codecContext->height = config.height;
        codecContext->time_base = AVRational{1, config.framerate};
        codecContext->framerate = AVRational{config.framerate, 1};
        codecContext->gop_size = 10;
        codecContext->max_b_frames = 1;
        codecContext->pix_fmt = AV_PIX_FMT_YUV420P;

        if (avcodec_open2(codecContext, codec, nullptr) < 0) {
            spdlog::error("Could not open codec");
            avcodec_free_context(&codecContext);
            m_isExporting = false;
            return;
        }

        AVFormatContext* formatContext = nullptr;
        avformat_alloc_output_context2(&formatContext, nullptr, nullptr, outputPath.c_str());
        if (!formatContext) {
            spdlog::error("Could not create output context");
            avcodec_free_context(&codecContext);
            m_isExporting = false;
            return;
        }

        AVStream* stream = avformat_new_stream(formatContext, nullptr);
        stream->time_base = codecContext->time_base;
        avcodec_parameters_from_context(stream->codecpar, codecContext);

        if (!(formatContext->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&formatContext->pb, outputPath.c_str(), AVIO_FLAG_WRITE) < 0) {
                spdlog::error("Could not open output file");
                avformat_free_context(formatContext);
                avcodec_free_context(&codecContext);
                m_isExporting = false;
                return;
            }
        }

        if (avformat_write_header(formatContext, nullptr) < 0) {
            spdlog::error("Could not write format header");
            if (!(formatContext->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&formatContext->pb);
            }
            avformat_free_context(formatContext);
            avcodec_free_context(&codecContext);
            m_isExporting = false;
            return;
        }

        AVFrame* frame = av_frame_alloc();
        frame->format = codecContext->pix_fmt;
        frame->width = codecContext->width;
        frame->height = codecContext->height;
        av_frame_get_buffer(frame, 0);

        SwsContext* swsContext = sws_getContext(
            config.width, config.height, AV_PIX_FMT_RGB24,
            config.width, config.height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        int totalFrames = static_cast<int>(config.length * config.framerate);
        float timePerFrame = 1.0f / config.framerate;
        float simulationTimePerFrame = 1.0f / config.tickrate;

        std::vector<unsigned char> pixels(config.width * config.height * 3);

        auto& simulation = Application::GetSimulation();
        simulation.Stop();
        simulation.Start();

        for (int frameIndex = 0; frameIndex < totalFrames; ++frameIndex) {
            m_currentTask = "Rendering frame " + std::to_string(frameIndex + 1) + "/" + std::to_string(totalFrames);
            m_progress = static_cast<float>(frameIndex) / totalFrames;
            if (progressCallback) progressCallback(m_progress);

            simulation.Update(simulationTimePerFrame);

            RenderFrame(scene, config.width, config.height);
            CaptureFramePixels(pixels, config.width, config.height);

            std::vector<unsigned char> flippedPixels = pixels;
            int rowSize = config.width * 3;
            std::vector<unsigned char> temp(rowSize);
            for (int y = 0; y < config.height / 2; ++y) {
                int topRow = y * rowSize;
                int bottomRow = (config.height - 1 - y) * rowSize;
                std::copy(flippedPixels.begin() + topRow, flippedPixels.begin() + topRow + rowSize, temp.begin());
                std::copy(flippedPixels.begin() + bottomRow, flippedPixels.begin() + bottomRow + rowSize, flippedPixels.begin() + topRow);
                std::copy(temp.begin(), temp.end(), flippedPixels.begin() + bottomRow);
            }

            const uint8_t* srcData[1] = { flippedPixels.data() };
            int srcLinesize[1] = { config.width * 3 };
            sws_scale(swsContext, srcData, srcLinesize, 0, config.height, frame->data, frame->linesize);

            frame->pts = frameIndex;

            AVPacket* pkt = av_packet_alloc();
            int ret = avcodec_send_frame(codecContext, frame);
            while (ret >= 0) {
                ret = avcodec_receive_packet(codecContext, pkt);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                if (ret < 0) {
                    spdlog::error("Error encoding frame");
                    break;
                }
                av_packet_rescale_ts(pkt, codecContext->time_base, stream->time_base);
                pkt->stream_index = stream->index;
                av_interleaved_write_frame(formatContext, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }

        avcodec_send_frame(codecContext, nullptr);
        AVPacket* pkt = av_packet_alloc();
        int ret;
        while ((ret = avcodec_receive_packet(codecContext, pkt)) >= 0) {
            av_packet_rescale_ts(pkt, codecContext->time_base, stream->time_base);
            pkt->stream_index = stream->index;
            av_interleaved_write_frame(formatContext, pkt);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);

        av_write_trailer(formatContext);

        sws_freeContext(swsContext);
        av_frame_free(&frame);
        
        if (!(formatContext->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&formatContext->pb);
        }
        
        avformat_free_context(formatContext);
        avcodec_free_context(&codecContext);

        m_currentTask = "Complete";
        m_progress = 1.0f;
        if (progressCallback) progressCallback(1.0f);

        spdlog::info("Video exported successfully to: {}", outputPath);

    } catch (const std::exception& e) {
        spdlog::error("Video export failed: {}", e.what());
        m_currentTask = "Failed";
    }

    m_camera.reset();
    CleanupOffscreenBuffers();
    
    m_isExporting = false;
}
