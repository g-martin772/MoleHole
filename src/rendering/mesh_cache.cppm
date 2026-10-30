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
        using Loader = std::function<std::shared_ptr<GPP::GltfSceneData>(const std::string&)>;

        MeshCache(std::shared_ptr<GPP::Renderer> renderer, std::shared_ptr<GPP::IFileSystem> fileSystem,
                  std::shared_ptr<GPP::Logger> logger)
            : MeshCache(fileSystem, logger,
                        [renderer, fileSystem, logger](const std::string& assetPath)
                        {
                            return std::make_shared<GPP::GltfSceneData>(
                                GPP::LoadGltfScene(renderer->GetDevice(), fileSystem, assetPath, logger));
                        },
                        [renderer] { return renderer->GetDevice(); })
        {
        }

        MeshCache(std::shared_ptr<GPP::IFileSystem> fileSystem, std::shared_ptr<GPP::Logger> logger, Loader loader,
                  std::function<std::shared_ptr<GPP::VulkanDevice>()> device = {})
            : m_FileSystem(std::move(fileSystem)), m_Logger(std::move(logger)), m_Loader(std::move(loader)),
              m_DeviceProvider(std::move(device)), m_Watch(m_FileSystem, m_Logger)
        {
            m_Worker = std::jthread([this](const std::stop_token& stop) { WorkerLoop(stop); });
        }

        ~MeshCache()
        {
            m_Worker.request_stop();
            m_Wake.notify_all();
        }

        MeshCache(const MeshCache&) = delete;
        MeshCache& operator=(const MeshCache&) = delete;

        std::shared_ptr<GPP::GltfSceneData> GetOrLoad(const std::string& assetPath)
        {
            if (const auto cached = TryGet(assetPath)) return cached.value_or(nullptr);
            const auto pathLock = [&]
            {
                std::scoped_lock lock(m_Mutex);
                auto& entry = m_PathLocks[assetPath];
                if (!entry) entry = std::make_shared<std::mutex>();
                return entry;
            }();
            std::scoped_lock loadLock(*pathLock);
            if (const auto cached = TryGet(assetPath)) return cached.value_or(nullptr);

            std::shared_ptr<GPP::GltfSceneData> result;
            try
            {
                result = m_Loader(assetPath);
            }
            catch (const std::exception& error)
            {
                m_Logger->Warn("MeshCache: failed to load mesh '{}': {}", assetPath, error.what());
            }
            Track(assetPath, result.get());
            std::scoped_lock lock(m_Mutex);
            m_Meshes.emplace(assetPath, result);
            m_Radii.emplace(assetPath, Radius(result.get()));
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

        void Update()
        {
            if (m_RetiredObjects && m_RetiredObjects->Pending() > 0) m_RetiredObjects->Collect();
            for (const auto& path : m_Watch.TakeChanged())
            {
                std::scoped_lock lock(m_Mutex);
                for (const auto& key : m_Paths.Find(path)) m_Stale.insert(key);
            }
            QueueStale();
            std::deque<Reloaded> done;
            {
                std::scoped_lock lock(m_JobMutex);
                done.swap(m_Done);
            }
            for (auto& result : done) Apply(std::move(result));
        }

        [[nodiscard]] std::uint64_t Generation() const noexcept { return m_Generation.load(); }

        [[nodiscard]] bool IsCurrent(const GPP::GltfSceneData* scene) const
        {
            std::scoped_lock lock(m_Mutex);
            return std::ranges::any_of(m_Meshes, [scene](const auto& entry) { return entry.second.get() == scene; });
        }

        void Retire(std::shared_ptr<void> object)
        {
            if (!m_RetiredObjects && m_DeviceProvider)
            {
                if (auto device = m_DeviceProvider()) m_RetiredObjects = std::make_unique<GPP::RetiredObjects>(std::move(device));
            }
            if (m_RetiredObjects) m_RetiredObjects->Retire(std::move(object));
        }

        [[nodiscard]] std::size_t PendingReloads() const
        {
            std::scoped_lock lock(m_JobMutex);
            return m_Jobs.size() + m_Done.size() + m_InFlight;
        }

    private:
        struct Reloaded
        {
            std::string AssetPath;
            std::shared_ptr<GPP::GltfSceneData> Scene;
            std::string Error;
        };

        static float Radius(const GPP::GltfSceneData* scene)
        {
            if (!scene || scene->Geometry.Vertices.empty()) return 0.5f;
            float maxDistSq = 0.0f;
            for (const auto& vertex : scene->Geometry.Vertices) maxDistSq = std::max(maxDistSq, glm::dot(vertex, vertex));
            return std::sqrt(maxDistSq);
        }

        void Track(const std::string& assetPath, const GPP::GltfSceneData* scene)
        {
            std::vector<std::filesystem::path> files{m_FileSystem->ResolvePath(assetPath)};
            if (scene) files.insert(files.end(), scene->Sources.begin(), scene->Sources.end());
            std::vector<std::filesystem::path> resolved;
            for (const auto& file : files) resolved.push_back(m_Watch.Watch(file));
            std::scoped_lock lock(m_Mutex);
            m_Paths.Remove(assetPath);
            for (const auto& file : resolved) m_Paths.Add(file, assetPath);
        }

        void QueueStale()
        {
            std::vector<std::string> ready;
            {
                std::scoped_lock lock(m_Mutex, m_JobMutex);
                for (auto it = m_Stale.begin(); it != m_Stale.end();)
                {
                    if (m_Active.contains(*it))
                    {
                        ++it;
                        continue;
                    }
                    m_Active.insert(*it);
                    m_Jobs.push_back(*it);
                    it = m_Stale.erase(it);
                }
            }
            m_Wake.notify_one();
        }

        void WorkerLoop(const std::stop_token& stop)
        {
            while (true)
            {
                std::string assetPath;
                {
                    std::unique_lock lock(m_JobMutex);
                    if (!m_Wake.wait(lock, stop, [this] { return !m_Jobs.empty(); })) return;
                    assetPath = std::move(m_Jobs.front());
                    m_Jobs.pop_front();
                    ++m_InFlight;
                }
                Reloaded result{.AssetPath = assetPath};
                try
                {
                    result.Scene = m_Loader(assetPath);
                    if (!result.Scene) result.Error = "empty result";
                }
                catch (const std::exception& error)
                {
                    result.Error = error.what();
                }
                std::scoped_lock lock(m_JobMutex);
                --m_InFlight;
                m_Done.push_back(std::move(result));
            }
        }

        void Apply(Reloaded result)
        {
            {
                std::scoped_lock lock(m_JobMutex);
                m_Active.erase(result.AssetPath);
            }
            const auto key = "mesh:" + result.AssetPath;
            if (!result.Scene)
            {
                m_Logger->Warn("MeshCache: reload of '{}' failed, keeping the previous mesh: {}", result.AssetPath,
                               result.Error);
                m_Logger->Notifications()->Post(GPP::NotificationKind::Error, "Mesh reload failed",
                                                result.AssetPath + "\n" + result.Error, 0.0f, key);
                return;
            }
            Track(result.AssetPath, result.Scene.get());
            std::shared_ptr<GPP::GltfSceneData> previous;
            {
                std::scoped_lock lock(m_Mutex);
                auto& slot = m_Meshes[result.AssetPath];
                previous = std::exchange(slot, result.Scene);
                m_Radii.insert_or_assign(result.AssetPath, Radius(result.Scene.get()));
            }
            m_Generation.fetch_add(1);
            Retire(std::move(previous));
            m_Logger->Info("MeshCache: reloaded '{}'", result.AssetPath);
            m_Logger->Notifications()->Post(GPP::NotificationKind::Info, "Mesh reloaded", result.AssetPath, 2.0f, key);
        }

        std::shared_ptr<GPP::IFileSystem> m_FileSystem;
        std::shared_ptr<GPP::Logger> m_Logger;
        Loader m_Loader;
        std::function<std::shared_ptr<GPP::VulkanDevice>()> m_DeviceProvider;
        std::unique_ptr<GPP::RetiredObjects> m_RetiredObjects;
        GPP::AssetFileWatch m_Watch;
        mutable std::mutex m_Mutex;
        std::unordered_map<std::string, std::shared_ptr<std::mutex>> m_PathLocks;
        std::unordered_map<std::string, std::shared_ptr<GPP::GltfSceneData>> m_Meshes;
        std::unordered_map<std::string, float> m_Radii;
        GPP::AssetPathIndex<std::string> m_Paths;
        std::set<std::string> m_Stale;
        std::atomic<std::uint64_t> m_Generation{0};

        mutable std::mutex m_JobMutex;
        std::condition_variable_any m_Wake;
        std::deque<std::string> m_Jobs;
        std::deque<Reloaded> m_Done;
        std::set<std::string> m_Active;
        std::size_t m_InFlight = 0;
        std::jthread m_Worker;
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
            AddFallback();
        }

        void Reset()
        {
            m_Library.Clear();
            AddFallback();
            m_WarnedOverflow = false;
            m_Dirty = true;
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
        void AddFallback()
        {
            GPP::GltfMaterial fallback;
            fallback.BaseColorFactor = {0.8f, 0.8f, 0.8f, 1.0f};
            fallback.MetallicFactor = 0.5f;
            fallback.RoughnessFactor = 0.5f;
            m_Library.AddMaterial(fallback);
        }

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
