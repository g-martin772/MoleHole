#pragma once
#include <string>
#include <memory>
#include <functional>
#include <shared_mutex>
#include <unordered_map>

// Forward declarations
class GLTFMesh;
struct GLFWwindow;

namespace Threading {

/**
 * Thread-safe mesh cache using shared_mutex for readers-writer lock
 */
class MeshCache {
public:
    MeshCache() = default;
    ~MeshCache() = default;
    
    // Get mesh from cache (multiple readers allowed)
    std::shared_ptr<GLTFMesh> get(const std::string& path);
    
    // Insert mesh into cache (exclusive writer)
    void insert(const std::string& path, std::shared_ptr<GLTFMesh> mesh);
    
    // Check if mesh exists in cache
    bool contains(const std::string& path) const;
    
    // Clear entire cache
    void clear();
    
    // Get cache size
    size_t size() const;
    
private:
    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<GLTFMesh>> m_cache;
};

/**
 * Resource loader for asynchronous loading of meshes and textures
 * Runs on a dedicated thread with shared OpenGL context
 */
class ResourceLoader {
public:
    ResourceLoader();
    ~ResourceLoader();
    
    // Initialize with shared GL context
    void initialize(GLFWwindow* mainContext);
    
    // Shutdown loader thread
    void shutdown();
    
    // Request async mesh load
    void requestMeshLoad(const std::string& path, int priority = 0);
    
    // Get mesh cache
    MeshCache& getMeshCache() { return m_meshCache; }
    const MeshCache& getMeshCache() const { return m_meshCache; }
    
    // Check if loader is running
    bool isRunning() const;
    
private:
    // Loader thread function
    void loaderThreadFunc();
    
    // Load mesh synchronously (called from loader thread)
    void loadMesh(const std::string& path);
    
    MeshCache m_meshCache;
    GLFWwindow* m_sharedContext = nullptr;
    
    // Thread state (managed by ThreadManager in full implementation)
    // For Phase 2, we'll keep it simple
    bool m_initialized = false;
};

} // namespace Threading
