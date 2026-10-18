export module MoleHole:Rendering.Meshes;

import std;
import glm;
import vulkan;
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

    // One material table and one texture table for every loaded glTF scene: a draw's material is
    // BaseMaterial(scene) + primitive.MaterialIndex, so a single descriptor set serves every draw. Index 0 is the
    // material of primitives without one. Without descriptor indexing the table holds 32 textures, the rest is untextured.
    class MeshMaterialTable
    {
    public:
        MeshMaterialTable(std::shared_ptr<GPP::VulkanDevice> device, std::shared_ptr<GPP::Logger> logger)
            : m_Device(std::move(device)), m_Logger(std::move(logger)),
              m_Library(GPP::GltfTextureCapacity(m_Device->GetCapabilities()))
        {
            GPP::GltfMaterial fallback;
            fallback.BaseColorFactor = {0.8f, 0.8f, 0.8f, 1.0f};
            fallback.MetallicFactor = 0.5f;
            fallback.RoughnessFactor = 0.5f;
            m_Library.AddMaterial(fallback);
        }

        [[nodiscard]] std::uint32_t BaseMaterial(const GPP::GltfSceneData& scene)
        {
            const auto before = m_Library.Materials().size();
            const auto base = m_Library.Add(scene);
            if (m_Library.Materials().size() != before) m_Dirty = true;
            if (m_Library.Overflowed() && !m_WarnedOverflow)
            {
                m_Logger->Warn("Mesh materials: more than {} textures without descriptor indexing, the rest are untextured",
                               m_Library.TextureCapacity());
                m_WarnedOverflow = true;
            }
            return base;
        }

        [[nodiscard]] static std::uint32_t MaterialIndex(const std::uint32_t base, const int primitiveMaterial) noexcept
        {
            return primitiveMaterial >= 0 ? base + static_cast<std::uint32_t>(primitiveMaterial) : 0u;
        }

        // Rebuilds the storage buffer and texture descriptors after BaseMaterial added a scene.
        void Sync(const GPP::VulkanImage& fallbackTexture)
        {
            if (!m_Dirty && m_Buffer) return;
            if (m_Buffer) m_Retired.push_back(std::move(m_Buffer));
            m_Buffer = std::make_shared<GPP::VulkanBuffer>(GPP::CreateMaterialBuffer(m_Device, m_Library, m_Logger));
            m_Textures = std::make_shared<const std::vector<vk::DescriptorImageInfo>>(
                GPP::MakeTextureDescriptors(m_Library, fallbackTexture));
            m_Dirty = false;
        }

        [[nodiscard]] const GPP::GltfMaterialGpu& Material(const std::uint32_t index) const
        {
            return m_Library.Materials()[std::min<std::size_t>(index, m_Library.Materials().size() - 1)];
        }
        [[nodiscard]] vk::Buffer Buffer() const { return m_Buffer ? m_Buffer->GetBuffer() : vk::Buffer{}; }
        [[nodiscard]] const std::shared_ptr<const std::vector<vk::DescriptorImageInfo>>& Textures() const { return m_Textures; }
        [[nodiscard]] std::uint32_t TextureCapacity() const noexcept { return m_Library.TextureCapacity(); }
        [[nodiscard]] const GPP::GltfMaterialLibrary& Library() const noexcept { return m_Library; }

    private:
        std::shared_ptr<GPP::VulkanDevice> m_Device;
        std::shared_ptr<GPP::Logger> m_Logger;
        GPP::GltfMaterialLibrary m_Library;
        std::shared_ptr<GPP::VulkanBuffer> m_Buffer;
        std::vector<std::shared_ptr<GPP::VulkanBuffer>> m_Retired; // frames in flight may still read the old table
        std::shared_ptr<const std::vector<vk::DescriptorImageInfo>> m_Textures;
        bool m_Dirty = true;
        bool m_WarnedOverflow = false;
    };
}
