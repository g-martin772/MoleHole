export module MoleHole:Rendering.Meshes;

import std;
import glm;
import GPP;

export namespace MoleHole
{
    class MeshCache
    {
    public:
        MeshCache(std::shared_ptr<GPP::Renderer> renderer, std::shared_ptr<GPP::IFileSystem> fileSystem,
                  std::shared_ptr<GPP::Logger> logger)
            : m_Renderer(std::move(renderer)), m_FileSystem(std::move(fileSystem)), m_Logger(std::move(logger))
        {
        }

        std::shared_ptr<GPP::GltfSceneData> GetOrLoad(const std::string& assetPath)
        {
            if (const auto cached = TryGet(assetPath)) return cached.value_or(nullptr);
            std::scoped_lock loadLock(m_LoadMutex); // one load at a time; a second caller waits and reuses it
            if (const auto cached = TryGet(assetPath)) return cached.value_or(nullptr);

            std::shared_ptr<GPP::GltfSceneData> result;
            float radius = 0.5f;
            try
            {
                result = std::make_shared<GPP::GltfSceneData>(
                    GPP::LoadGltfScene(m_Renderer->GetDevice(), m_FileSystem, assetPath, m_Logger));
                if (!result->Geometry.Vertices.empty())
                {
                    float maxDistSq = 0.0f;
                    for (const auto& vertex : result->Geometry.Vertices)
                    {
                        maxDistSq = std::max(maxDistSq, glm::dot(vertex, vertex));
                    }
                    radius = std::sqrt(maxDistSq);
                }
            }
            catch (const std::exception& error)
            {
                m_Logger->Warn("MeshCache: failed to load mesh '{}': {}", assetPath, error.what());
            }
            std::scoped_lock lock(m_Mutex);
            m_Meshes.emplace(assetPath, result);
            m_Radii.emplace(assetPath, radius);
            return result;
        }

        std::optional<std::shared_ptr<GPP::GltfSceneData>> TryGet(const std::string& assetPath) const
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Meshes.find(assetPath);
            if (it == m_Meshes.end()) return std::nullopt;
            return it->second;
        }

        float BoundingRadius(const std::string& assetPath) const
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Radii.find(assetPath);
            return it != m_Radii.end() ? it->second : 0.5f;
        }

    private:
        std::shared_ptr<GPP::Renderer> m_Renderer;
        std::shared_ptr<GPP::IFileSystem> m_FileSystem;
        std::shared_ptr<GPP::Logger> m_Logger;
        mutable std::mutex m_Mutex;
        std::mutex m_LoadMutex;
        std::unordered_map<std::string, std::shared_ptr<GPP::GltfSceneData>> m_Meshes;
        std::unordered_map<std::string, float> m_Radii;
    };
}
