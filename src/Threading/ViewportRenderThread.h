#pragma once
#include <thread>
#include <atomic>
#include <memory>
#include "CommandTypes.h"
#include "ThreadSafeQueue.h"

// Forward declarations
struct GLFWwindow;
class Scene;
class BlackHoleRenderer;
class VisualRenderer;
class GravityGridRenderer;

// OpenGL types (avoid including gl.h here)
typedef unsigned int GLuint;

namespace Threading {

/**
 * Viewport render thread - renders the 3D viewport independently from UI
 * Runs at 30-60 FPS while UI maintains 60+ Hz
 */
class ViewportRenderThread {
public:
    ViewportRenderThread();
    ~ViewportRenderThread();
    
    // Initialize with shared context
    void initialize(GLFWwindow* mainContext);
    
    // Start the render thread
    void start();
    
    // Stop the render thread
    void stop();
    
    // Check if thread is running
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }
    
    // Dispatch render command
    void dispatchCommand(RenderCommand cmd);
    
    // Get the completed viewport texture
    GLuint getCompletedTexture() const { return m_completedTexture.load(std::memory_order_acquire); }
    
    // Get viewport dimensions
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    
    // Set viewport size (thread-safe)
    void setViewportSize(int width, int height);
    
    // Prevent copying
    ViewportRenderThread(const ViewportRenderThread&) = delete;
    ViewportRenderThread& operator=(const ViewportRenderThread&) = delete;
    
private:
    // Thread function
    void renderThreadFunc();
    
    // Render one frame
    void renderFrame(const RenderCommand& cmd, Scene* scene);
    
    // Setup FBO and textures
    void setupFramebuffers();
    void cleanupFramebuffers();
    
    // Shared OpenGL context
    GLFWwindow* m_sharedContext = nullptr;
    
    // Thread
    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_initialized{false};
    
    // Command queue
    ThreadSafeQueue<RenderCommand> m_commandQueue;
    
    // Framebuffers (double buffered)
    GLuint m_fbos[2] = {0, 0};
    GLuint m_textures[2] = {0, 0};
    GLuint m_depthRBOs[2] = {0, 0};
    int m_currentFBO = 0;
    
    // Completed texture (atomic for lock-free access)
    std::atomic<GLuint> m_completedTexture{0};
    
    // Viewport dimensions
    std::atomic<int> m_width{800};
    std::atomic<int> m_height{600};
    bool m_resizePending = false;
    
    // Renderers (created on render thread)
    std::unique_ptr<BlackHoleRenderer> m_blackHoleRenderer;
    std::unique_ptr<VisualRenderer> m_visualRenderer;
    std::unique_ptr<GravityGridRenderer> m_gravityGridRenderer;
};

} // namespace Threading
