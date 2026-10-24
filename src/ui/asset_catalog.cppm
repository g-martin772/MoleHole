export module MoleHole:UI.AssetCatalog;

import std;
import GPP;
import :UI.FuzzySearch;

export namespace MoleHole
{
    struct AssetEntry
    {
        std::string Path;
        std::uintmax_t Size = 0;

        [[nodiscard]] std::string Name() const { return std::filesystem::path(Path).filename().generic_string(); }
    };

    struct AssetScanConfig
    {
        std::vector<std::filesystem::path> Roots;
        std::filesystem::path ProjectRoot;

        bool operator==(const AssetScanConfig&) const = default;
    };

    [[nodiscard]] inline std::span<const std::string_view> AssetExtensions(const GPP::AssetFieldKind kind)
    {
        static constexpr std::array<std::string_view, 9> textures{".png", ".jpg", ".jpeg", ".tga", ".bmp", ".psd",
                                                                  ".hdr", ".pic", ".gif"};
        static constexpr std::array<std::string_view, 2> meshes{".gltf", ".glb"};
        static constexpr std::array<std::string_view, 2> scripts{".lua", ".luau"};
        static constexpr std::array<std::string_view, 4> audio{".wav", ".ogg", ".mp3", ".flac"};
        switch (kind)
        {
            case GPP::AssetFieldKind::Texture: return textures;
            case GPP::AssetFieldKind::Mesh: return meshes;
            case GPP::AssetFieldKind::Script: return scripts;
            case GPP::AssetFieldKind::Audio: return audio;
            default: return {};
        }
    }

    [[nodiscard]] inline bool AssetMatchesKind(const std::filesystem::path& path, const GPP::AssetFieldKind kind)
    {
        const auto extensions = AssetExtensions(kind);
        if (extensions.empty()) return true;
        auto extension = path.extension().string();
        std::ranges::transform(extension, extension.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return std::ranges::contains(extensions, std::string_view(extension));
    }

    [[nodiscard]] inline std::string NormalizeAssetPath(const std::filesystem::path& file, const std::filesystem::path& projectRoot)
    {
        std::error_code ec;
        const auto absolute = std::filesystem::weakly_canonical(file, ec);
        if (ec) return file.generic_string();
        if (!projectRoot.empty())
        {
            const auto root = std::filesystem::weakly_canonical(projectRoot, ec);
            const auto relative = ec ? std::filesystem::path{} : absolute.lexically_relative(root);
            if (!relative.empty() && *relative.begin() != "..") return relative.generic_string();
        }
        return absolute.generic_string();
    }

    [[nodiscard]] inline std::vector<AssetEntry> ScanAssets(const AssetScanConfig& config, const GPP::AssetFieldKind kind,
                                                            const std::stop_token stop = {}, const std::size_t limit = 20000)
    {
        std::vector<AssetEntry> result;
        std::error_code ec;
        for (const auto& root : config.Roots)
        {
            if (!std::filesystem::is_directory(root, ec)) continue;
            std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec);
            for (const std::filesystem::recursive_directory_iterator end; !ec && it != end; it.increment(ec))
            {
                if (stop.stop_requested() || result.size() >= limit) return result;
                const auto& entry = *it;
                const auto name = entry.path().filename().string();
                if (entry.is_directory(ec))
                {
                    if (name.starts_with('.') || name == "node_modules") it.disable_recursion_pending();
                    continue;
                }
                if (name.starts_with('.') || !entry.is_regular_file(ec) || !AssetMatchesKind(entry.path(), kind)) continue;
                result.push_back({NormalizeAssetPath(entry.path(), config.ProjectRoot), entry.file_size(ec)});
            }
        }
        std::ranges::sort(result, {}, &AssetEntry::Path);
        const auto duplicates = std::ranges::unique(result, {}, &AssetEntry::Path);
        result.erase(duplicates.begin(), duplicates.end());
        return result;
    }

    [[nodiscard]] inline std::vector<std::size_t> RankAssets(const std::span<const AssetEntry> entries, const std::string_view query,
                                                             const std::span<const std::string> recents = {})
    {
        std::vector<std::pair<int, std::size_t>> scored;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            int bonus = 0;
            if (const auto recent = std::ranges::find(recents, entries[i].Path); recent != recents.end())
            {
                bonus = 40 + static_cast<int>(recents.size()) - static_cast<int>(recent - recents.begin());
            }
            if (query.find_first_not_of(" \t") == std::string_view::npos)
            {
                scored.emplace_back(bonus, i);
                continue;
            }
            const auto byName = FuzzyScore(query, entries[i].Name());
            const auto byPath = FuzzyScore(query, entries[i].Path);
            if (!byName && !byPath) continue;
            scored.emplace_back(std::max(byName ? *byName + 60 : 0, byPath.value_or(0)) + bonus, i);
        }
        std::ranges::stable_sort(scored, [](const auto& a, const auto& b) { return a.first > b.first; });
        std::vector<std::size_t> result;
        result.reserve(scored.size());
        for (const auto& [score, index] : scored) result.push_back(index);
        return result;
    }

    struct AssetFolderView
    {
        std::vector<std::pair<std::string, std::size_t>> Folders;
        std::vector<std::size_t> Files;
    };

    [[nodiscard]] inline AssetFolderView ListAssetFolder(const std::span<const AssetEntry> entries, const std::string_view folder)
    {
        AssetFolderView view;
        const std::string prefix = folder.empty() ? std::string{} : std::string(folder) + '/';
        std::map<std::string, std::size_t> folders;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            std::string_view rest = entries[i].Path;
            if (!prefix.empty())
            {
                if (!rest.starts_with(prefix)) continue;
                rest.remove_prefix(prefix.size());
            }
            if (const auto slash = rest.find('/'); slash == std::string_view::npos) view.Files.push_back(i);
            else ++folders[std::string(rest.substr(0, slash))];
        }
        view.Folders.assign(folders.begin(), folders.end());
        return view;
    }

    [[nodiscard]] inline std::string FormatAssetSize(const std::uintmax_t bytes)
    {
        if (bytes < 1024) return std::format("{} B", bytes);
        if (bytes < 1024 * 1024) return std::format("{:.0f} KB", static_cast<double>(bytes) / 1024.0);
        if (bytes < 1024ull * 1024 * 1024) return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        return std::format("{:.2f} GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }

    [[nodiscard]] inline std::vector<std::string> PushRecentAsset(std::vector<std::string> recents, const std::string& path,
                                                                  const std::size_t limit = 12)
    {
        if (path.empty()) return recents;
        std::erase(recents, path);
        recents.insert(recents.begin(), path);
        if (recents.size() > limit) recents.resize(limit);
        return recents;
    }

    class AssetCatalog
    {
    public:
        using Entries = std::shared_ptr<const std::vector<AssetEntry>>;

        struct Snapshot
        {
            Entries Items;
            bool Scanning = false;
        };

        AssetCatalog() : m_Worker([this](const std::stop_token stop) { Run(stop); }) {}

        ~AssetCatalog()
        {
            m_Worker.request_stop();
            m_Wake.notify_all();
        }

        AssetCatalog(const AssetCatalog&) = delete;
        AssetCatalog& operator=(const AssetCatalog&) = delete;

        Snapshot Get(const GPP::AssetFieldKind kind, const AssetScanConfig& config, const bool refresh = false,
                     const std::chrono::milliseconds maxAge = std::chrono::milliseconds(3000))
        {
            std::scoped_lock lock(m_Mutex);
            auto& cache = m_Cache[kind];
            const auto now = std::chrono::steady_clock::now();
            const bool stale = !cache.Valid || cache.Config != config || now - cache.Scanned > maxAge;
            if ((refresh || stale) && !cache.Pending)
            {
                cache.Pending = true;
                cache.Requested = config;
                m_Queue.push_back(kind);
                m_Wake.notify_all();
            }
            return {cache.Items, cache.Pending};
        }

    private:
        struct Cache
        {
            Entries Items;
            AssetScanConfig Config;
            AssetScanConfig Requested;
            std::chrono::steady_clock::time_point Scanned{};
            bool Valid = false;
            bool Pending = false;
        };

        void Run(const std::stop_token stop)
        {
            while (!stop.stop_requested())
            {
                GPP::AssetFieldKind kind{};
                AssetScanConfig config;
                {
                    std::unique_lock lock(m_Mutex);
                    m_Wake.wait(lock, stop, [this] { return !m_Queue.empty(); });
                    if (stop.stop_requested()) return;
                    kind = m_Queue.front();
                    m_Queue.pop_front();
                    config = m_Cache[kind].Requested;
                }
                auto items = std::make_shared<const std::vector<AssetEntry>>(ScanAssets(config, kind, stop));
                std::scoped_lock lock(m_Mutex);
                auto& cache = m_Cache[kind];
                cache.Items = std::move(items);
                cache.Config = std::move(config);
                cache.Scanned = std::chrono::steady_clock::now();
                cache.Valid = true;
                cache.Pending = false;
            }
        }

        std::mutex m_Mutex;
        std::condition_variable_any m_Wake;
        std::map<GPP::AssetFieldKind, Cache> m_Cache;
        std::deque<GPP::AssetFieldKind> m_Queue;
        std::jthread m_Worker;
    };
}
