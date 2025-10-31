#include "ResourceLoader.h"
#include "Renderer/GLTFMesh.h"
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

namespace Threading {

// MeshCache implementation

std::shared_ptr<GLTFMesh> MeshCache::get(const std::string& path) {
    std::shared_lock lock(m_mutex); // Multiple readers
    auto it = m_cache.find(path);
    return (it != m_cache.end()) ? it->second : nullptr;
}

void MeshCache::insert(const std::string& path, std::shared_ptr<GLTFMesh> mesh) {
    std::unique_lock lock(m_mutex); // Exclusive writer
    m_cache[path] = mesh;
}

bool MeshCache::contains(const std::string& path) const {
    std::shared_lock lock(m_mutex);
    return m_cache.find(path) != m_cache.end();
}

void MeshCache::clear() {
    std::unique_lock lock(m_mutex);
    m_cache.clear();
}

size_t MeshCache::size() const {
    std::shared_lock lock(m_mutex);
    return m_cache.size();
}

// ResourceLoader implementation

ResourceLoader::ResourceLoader() {
    spdlog::debug("ResourceLoader constructor");
}

ResourceLoader::~ResourceLoader() {
    shutdown();
}

void ResourceLoader::initialize(GLFWwindow* mainContext) {
    if (m_initialized) {
        spdlog::warn("ResourceLoader already initialized");
        return;
    }
    
    spdlog::info("Initializing ResourceLoader (Phase 2 - basic version)");
    
    // For Phase 2, we'll keep the loader simple and synchronous
    // Full async implementation will come in later phases
    m_initialized = true;
    
    spdlog::info("ResourceLoader initialized successfully");
}

void ResourceLoader::shutdown() {
    if (!m_initialized) {
        return;
    }
    
    spdlog::info("Shutting down ResourceLoader");
    
    // Clear mesh cache
    m_meshCache.clear();
    
    // Cleanup shared context if created
    if (m_sharedContext) {
        glfwDestroyWindow(m_sharedContext);
        m_sharedContext = nullptr;
    }
    
    m_initialized = false;
    
    spdlog::info("ResourceLoader shutdown complete");
}

bool ResourceLoader::isRunning() const {
    return m_initialized;
}

void ResourceLoader::requestMeshLoad(const std::string& path, int priority) {
    // Phase 2: Synchronous loading for now
    // Full async implementation will be added in Phase 3
    
    // Check if already in cache
    if (m_meshCache.contains(path)) {
        spdlog::debug("Mesh already in cache: {}", path);
        return;
    }
    
    spdlog::debug("Loading mesh synchronously: {}", path);
    loadMesh(path);
}

void ResourceLoader::loadMesh(const std::string& path) {
    // Create and load mesh
    auto mesh = std::make_shared<GLTFMesh>();
    
    if (mesh->Load(path)) {
        // Add to cache
        m_meshCache.insert(path, mesh);
        spdlog::info("Loaded mesh: {}", path);
    } else {
        spdlog::error("Failed to load mesh: {}", path);
    }
}

void ResourceLoader::loaderThreadFunc() {
    // Phase 2: Placeholder for future async implementation
    // Will be fully implemented in Phase 3
    spdlog::info("Resource loader thread started (not yet implemented)");
    
    // TODO: Implement async loading loop
    
    spdlog::info("Resource loader thread stopped");
}

} // namespace Threading
