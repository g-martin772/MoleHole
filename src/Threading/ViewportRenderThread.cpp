#include "ViewportRenderThread.h"
#include <glad/gl.h>  // Must be before GLFW
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>
#include <cmath>
#include "Renderer/BlackHoleRenderer.h"
#include "Renderer/VisualRenderer.h"
#include "Renderer/GravityGridRenderer.h"
#include "Simulation/Scene.h"

namespace Threading {

ViewportRenderThread::ViewportRenderThread() {
    spdlog::debug("ViewportRenderThread constructor");
}

ViewportRenderThread::~ViewportRenderThread() {
    stop();
}

void ViewportRenderThread::initialize(GLFWwindow* mainContext) {
    if (m_initialized.load(std::memory_order_acquire)) {
        spdlog::warn("ViewportRenderThread already initialized");
        return;
    }
    
    spdlog::info("Initializing ViewportRenderThread (Phase 3)");
    
    // Create shared context (invisible window)
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    m_sharedContext = glfwCreateWindow(1, 1, "", nullptr, mainContext);
    
    if (!m_sharedContext) {
        spdlog::error("Failed to create shared OpenGL context for viewport render thread");
        return;
    }
    
    spdlog::info("Created shared OpenGL context for viewport render thread");
    
    m_initialized.store(true, std::memory_order_release);
    
    spdlog::info("ViewportRenderThread initialized successfully");
}

void ViewportRenderThread::start() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        spdlog::error("ViewportRenderThread not initialized - call initialize() first");
        return;
    }
    
    if (m_running.load(std::memory_order_acquire)) {
        spdlog::warn("ViewportRenderThread already running");
        return;
    }
    
    m_running.store(true, std::memory_order_release);
    m_thread = std::make_unique<std::thread>(&ViewportRenderThread::renderThreadFunc, this);
    
    spdlog::info("ViewportRenderThread started");
}

void ViewportRenderThread::stop() {
    if (!m_running.load(std::memory_order_acquire)) {
        return;
    }
    
    spdlog::info("Stopping ViewportRenderThread");
    
    m_running.store(false, std::memory_order_release);
    m_commandQueue.shutdown();
    
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
    
    // Cleanup shared context
    if (m_sharedContext) {
        glfwDestroyWindow(m_sharedContext);
        m_sharedContext = nullptr;
    }
    
    m_initialized.store(false, std::memory_order_release);
    
    spdlog::info("ViewportRenderThread stopped");
}

void ViewportRenderThread::dispatchCommand(RenderCommand cmd) {
    if (m_running.load(std::memory_order_acquire)) {
        m_commandQueue.push(std::move(cmd));
    }
}

void ViewportRenderThread::setViewportSize(int width, int height) {
    m_width.store(width, std::memory_order_release);
    m_height.store(height, std::memory_order_release);
    m_resizePending = true;
}

void ViewportRenderThread::renderThreadFunc() {
    spdlog::info("Viewport render thread started");
    
    // Make shared context current on this thread
    glfwMakeContextCurrent(m_sharedContext);
    
    // Re-initialize GLAD on this thread
    int version = gladLoadGL(glfwGetProcAddress);
    if (version == 0) {
        spdlog::error("Failed to initialize GLAD on render thread");
        return;
    }
    
    spdlog::info("Viewport render thread - OpenGL {}.{}", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));
    
    // Create renderers on this thread
    m_blackHoleRenderer = std::make_unique<BlackHoleRenderer>();
    m_blackHoleRenderer->Init(m_width.load(), m_height.load());
    
    m_visualRenderer = std::make_unique<VisualRenderer>();
    m_visualRenderer->Init(m_width.load(), m_height.load());
    
    m_gravityGridRenderer = std::make_unique<GravityGridRenderer>();
    m_gravityGridRenderer->Init();
    
    // Setup framebuffers
    setupFramebuffers();
    
    // Render loop
    while (m_running.load(std::memory_order_acquire)) {
        // Wait for render command
        auto cmd = m_commandQueue.pop();
        if (!cmd.has_value()) {
            break; // Shutdown signal
        }
        
        // Handle resize if needed
        if (m_resizePending) {
            int newWidth = m_width.load(std::memory_order_acquire);
            int newHeight = m_height.load(std::memory_order_acquire);
            
            spdlog::debug("Viewport render thread resizing to {}x{}", newWidth, newHeight);
            
            cleanupFramebuffers();
            setupFramebuffers();
            
            if (m_blackHoleRenderer) {
                m_blackHoleRenderer->Resize(newWidth, newHeight);
            }
            if (m_visualRenderer) {
                m_visualRenderer->Resize(newWidth, newHeight);
            }
            
            m_resizePending = false;
        }
        
        // TODO: Get scene from triple buffer (Phase 4)
        // For now, we'll just render to the FBO
        Scene* scene = nullptr; // Will be implemented in Phase 4
        
        renderFrame(cmd.value(), scene);
    }
    
    // Cleanup
    cleanupFramebuffers();
    
    m_blackHoleRenderer.reset();
    m_visualRenderer.reset();
    m_gravityGridRenderer.reset();
    
    spdlog::info("Viewport render thread stopped");
}

void ViewportRenderThread::renderFrame(const RenderCommand& cmd, Scene* scene) {
    // Bind current FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbos[m_currentFBO]);
    
    int width = m_width.load(std::memory_order_relaxed);
    int height = m_height.load(std::memory_order_relaxed);
    
    glViewport(0, 0, width, height);
    
    // Clear
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // TODO: Actual rendering implementation (Phase 3 continued)
    // For now, just clear to a color to verify it's working
    float t = static_cast<float>(glfwGetTime());
    glClearColor(0.5f + 0.5f * std::sin(t), 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Unbind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Mark texture as complete (atomic write)
    m_completedTexture.store(m_textures[m_currentFBO], std::memory_order_release);
    
    // Swap to other FBO for next frame
    m_currentFBO = (m_currentFBO + 1) % 2;
}

void ViewportRenderThread::setupFramebuffers() {
    int width = m_width.load(std::memory_order_relaxed);
    int height = m_height.load(std::memory_order_relaxed);
    
    spdlog::debug("Setting up viewport framebuffers {}x{}", width, height);
    
    // Create double-buffered FBOs
    for (int i = 0; i < 2; i++) {
        // Create FBO
        glGenFramebuffers(1, &m_fbos[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbos[i]);
        
        // Create texture
        glGenTextures(1, &m_textures[i]);
        glBindTexture(GL_TEXTURE_2D, m_textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textures[i], 0);
        
        // Create depth/stencil RBO
        glGenRenderbuffers(1, &m_depthRBOs[i]);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depthRBOs[i]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthRBOs[i]);
        
        // Check framebuffer
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            spdlog::error("Viewport render thread: Framebuffer {} is not complete!", i);
        }
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Set initial completed texture
    m_completedTexture.store(m_textures[0], std::memory_order_release);
}

void ViewportRenderThread::cleanupFramebuffers() {
    for (int i = 0; i < 2; i++) {
        if (m_depthRBOs[i]) {
            glDeleteRenderbuffers(1, &m_depthRBOs[i]);
            m_depthRBOs[i] = 0;
        }
        if (m_textures[i]) {
            glDeleteTextures(1, &m_textures[i]);
            m_textures[i] = 0;
        }
        if (m_fbos[i]) {
            glDeleteFramebuffers(1, &m_fbos[i]);
            m_fbos[i] = 0;
        }
    }
    
    m_completedTexture.store(0, std::memory_order_release);
}

} // namespace Threading
