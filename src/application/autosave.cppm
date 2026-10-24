export module MoleHole:Application.Autosave;

import std;
import GPP;

export namespace MoleHole
{
    inline constexpr std::array kAutosaveDelaysSeconds{1, 5, 30};
    inline constexpr int kDefaultAutosaveDelaySeconds = 5;

    [[nodiscard]] inline std::string FormatAutosaveDelay(const int seconds) { return std::format("{} s", seconds); }

    class AutosaveScheduler
    {
    public:
        using Clock = std::chrono::steady_clock;

        enum class Action { None, SaveFile, WriteRecovery };

        struct Config
        {
            bool FileEnabled = false;
            Clock::duration Delay = std::chrono::seconds(kDefaultAutosaveDelaySeconds);
            Clock::duration RecoveryInterval = std::chrono::seconds(30);
            Clock::duration BackoffBase = std::chrono::seconds(2);
            Clock::duration BackoffMax = std::chrono::seconds(60);
        };

        struct Input
        {
            Clock::time_point Now;
            bool Dirty = false;
            std::uint64_t Revision = 0;
            bool Playing = false;
            bool ModalOpen = false;
            bool HasPath = false;
        };

        void Configure(const Config& config) noexcept { m_Config = config; }
        [[nodiscard]] const Config& Settings() const noexcept { return m_Config; }

        [[nodiscard]] Action Update(const Input& in)
        {
            if (!in.Dirty)
            {
                m_Dirty = false;
                m_RecoveryDue.reset();
                m_RecoveredRevision = kNone;
                return Action::None;
            }
            if (!m_Dirty)
            {
                m_Dirty = true;
                m_Revision = in.Revision;
                m_Changed = in.Now;
                m_RecoveryDue = in.Now + m_Config.RecoveryInterval;
            }
            else if (in.Revision != m_Revision)
            {
                m_Revision = in.Revision;
                m_Changed = in.Now;
            }
            if (in.Playing || in.ModalOpen) return Action::None;

            if (m_Config.FileEnabled && in.HasPath && in.Now >= m_Changed + m_Config.Delay && in.Now >= m_FileNotBefore)
                return Action::SaveFile;
            if (m_RecoveredRevision != m_Revision && m_RecoveryDue && in.Now >= *m_RecoveryDue)
                return Action::WriteRecovery;
            return Action::None;
        }

        void Succeeded(const Action action, const Clock::time_point now)
        {
            if (action == Action::SaveFile)
            {
                m_Failures[0] = 0;
                m_FileNotBefore = {};
            }
            else if (action == Action::WriteRecovery)
            {
                m_Failures[1] = 0;
                m_RecoveredRevision = m_Revision;
                m_RecoveryDue = now + m_Config.RecoveryInterval;
            }
        }

        int Failed(const Action action, const Clock::time_point now)
        {
            if (action == Action::None) return 0;
            const bool file = action == Action::SaveFile;
            const int streak = ++m_Failures[file ? 0 : 1];
            const auto wait = Backoff(streak);
            if (file) m_FileNotBefore = now + wait;
            else m_RecoveryDue = now + wait;
            return streak;
        }

        [[nodiscard]] bool Failing() const noexcept { return m_Failures[0] > 0 || m_Failures[1] > 0; }

        [[nodiscard]] Clock::duration Backoff(const int streak) const
        {
            auto wait = m_Config.BackoffBase;
            for (int i = 1; i < streak && wait < m_Config.BackoffMax; ++i) wait *= 2;
            return std::min(wait, m_Config.BackoffMax);
        }

    private:
        static constexpr std::uint64_t kNone = std::numeric_limits<std::uint64_t>::max();

        Config m_Config;
        bool m_Dirty = false;
        std::uint64_t m_Revision = 0;
        std::uint64_t m_RecoveredRevision = kNone;
        Clock::time_point m_Changed{};
        Clock::time_point m_FileNotBefore{};
        std::optional<Clock::time_point> m_RecoveryDue;
        std::array<int, 2> m_Failures{};
    };

    struct RecoveryEntry
    {
        std::filesystem::path File;
        std::string ScenePath;
        std::string SceneName;
        long ProcessId = 0;
        std::int64_t Modified = 0;
    };

    struct RecoveryHeader
    {
        long ProcessId = 0;
        std::string ScenePath;
        std::string SceneName;
    };

    [[nodiscard]] inline std::string RecoveryKey(const std::string_view scenePath, const std::string_view sceneName)
    {
        std::uint64_t hash = 1469598103934665603ull;
        const std::string seed = scenePath.empty() ? "untitled:" + std::string(sceneName) : std::string(scenePath);
        for (const char c : seed)
        {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ull;
        }
        return std::format("{:016x}", hash);
    }

    [[nodiscard]] inline std::string RecoveryHeaderText(const RecoveryHeader& header)
    {
        const auto line = [](std::string text)
        {
            std::ranges::replace(text, '\n', ' ');
            std::ranges::replace(text, '\r', ' ');
            return text;
        };
        return std::format("# molehole-recovery 1\n# pid: {}\n# path: {}\n# name: {}\n", header.ProcessId,
                           line(header.ScenePath), line(header.SceneName));
    }

    [[nodiscard]] inline std::optional<RecoveryHeader> ParseRecoveryHeader(const std::string_view text)
    {
        constexpr std::string_view kMagic = "# molehole-recovery";
        if (!text.starts_with(kMagic)) return std::nullopt;
        RecoveryHeader header;
        bool hasPid = false;
        std::size_t pos = text.find('\n');
        while (pos != std::string_view::npos && pos + 1 < text.size() && text[pos + 1] == '#')
        {
            const std::size_t begin = pos + 1;
            const std::size_t end = text.find('\n', begin);
            auto line = text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
            const auto take = [&](const std::string_view key) -> std::optional<std::string_view>
            {
                const std::string prefix = std::format("# {}: ", key);
                if (!line.starts_with(prefix)) return std::nullopt;
                return line.substr(prefix.size());
            };
            if (const auto v = take("pid"))
            {
                const auto [_, ec] = std::from_chars(v->data(), v->data() + v->size(), header.ProcessId);
                hasPid = ec == std::errc{};
            }
            else if (const auto v2 = take("path")) header.ScenePath = *v2;
            else if (const auto v3 = take("name")) header.SceneName = *v3;
            pos = end;
        }
        if (!hasPid) return std::nullopt;
        return header;
    }

    [[nodiscard]] inline std::string StripRecoveryHeader(const std::string_view text)
    {
        std::size_t pos = 0;
        while (pos < text.size() && text[pos] == '#')
        {
            const auto end = text.find('\n', pos);
            if (end == std::string_view::npos) return {};
            pos = end + 1;
        }
        return std::string(text.substr(pos));
    }

    [[nodiscard]] inline bool IsRecoveryNewer(const std::filesystem::file_time_type recovery,
                                              const std::optional<std::filesystem::file_time_type> sceneFile)
    {
        return !sceneFile || recovery > *sceneFile;
    }

    // One file per instance (`<hash>-<pid>.yaml`) so a crashed instance's copy is never overwritten
    class RecoveryStore
    {
    public:
        using AliveCheck = std::function<bool(long)>;

        RecoveryStore() = default;

        explicit RecoveryStore(std::filesystem::path directory, const long processId,
                               AliveCheck alive = [](const long pid) { return GPP::IsInstanceOfSelf(pid); })
        {
            Configure(std::move(directory), processId, std::move(alive));
        }

        void Configure(std::filesystem::path directory, const long processId,
                       AliveCheck alive = [](const long pid) { return GPP::IsInstanceOfSelf(pid); })
        {
            m_Directory = std::move(directory);
            m_ProcessId = processId;
            m_Alive = std::move(alive);
        }

        [[nodiscard]] bool Enabled() const noexcept { return !m_Directory.empty(); }
        [[nodiscard]] const std::filesystem::path& Directory() const noexcept { return m_Directory; }

        [[nodiscard]] std::filesystem::path FileFor(const std::string_view scenePath, const std::string_view sceneName) const
        {
            return m_Directory / std::format("{}-{}.yaml", RecoveryKey(scenePath, sceneName), m_ProcessId);
        }

        [[nodiscard]] std::expected<void, std::string> Write(const std::string_view scenePath,
                                                              const std::string_view sceneName, const std::string_view yaml)
        {
            if (!Enabled()) return std::unexpected("recovery directory not configured");
            const auto file = FileFor(scenePath, sceneName);
            const auto text = RecoveryHeaderText({m_ProcessId, std::string(scenePath), std::string(sceneName)})
                + std::string(yaml);
            if (auto written = GPP::WriteFileAtomic(file, text); !written) return written;
            std::scoped_lock lock(m_Mutex);
            if (!m_Last.empty() && m_Last != file) Remove(m_Last);
            m_Last = file;
            return {};
        }

        void Clear()
        {
            std::scoped_lock lock(m_Mutex);
            if (m_Last.empty()) return;
            Remove(m_Last);
            m_Last.clear();
        }

        [[nodiscard]] bool HasWritten() const
        {
            std::scoped_lock lock(m_Mutex);
            return !m_Last.empty();
        }

        [[nodiscard]] std::expected<void, std::string> Adopt(const std::filesystem::path& file)
        {
            std::ifstream in(file, std::ios::binary);
            if (!in) return std::unexpected(std::format("cannot read '{}'", file.string()));
            const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            in.close();
            const auto header = ParseRecoveryHeader(text);
            if (!header) return std::unexpected(std::format("'{}' is not a recovery copy", file.string()));
            const auto own = FileFor(header->ScenePath, header->SceneName);
            if (auto written = Write(header->ScenePath, header->SceneName, StripRecoveryHeader(text)); !written) return written;
            if (own != file) Remove(file);
            return {};
        }

        [[nodiscard]] std::vector<RecoveryEntry> Scan() const
        {
            std::vector<RecoveryEntry> entries;
            std::error_code ec;
            if (!Enabled() || !std::filesystem::is_directory(m_Directory, ec)) return entries;
            for (const auto& item : std::filesystem::directory_iterator(m_Directory, ec))
            {
                if (ec) break;
                if (!item.is_regular_file(ec) || item.path().extension() != ".yaml") continue;
                const auto header = ReadHeader(item.path());
                if (!header || header->ProcessId == m_ProcessId || (m_Alive && m_Alive(header->ProcessId))) continue;

                const auto modified = item.last_write_time(ec);
                std::optional<std::filesystem::file_time_type> sceneTime;
                if (!header->ScenePath.empty())
                {
                    std::error_code sceneEc;
                    const auto t = std::filesystem::last_write_time(header->ScenePath, sceneEc);
                    if (!sceneEc) sceneTime = t;
                }
                if (!IsRecoveryNewer(modified, sceneTime))
                {
                    Remove(item.path());
                    continue;
                }
                entries.push_back({item.path(), header->ScenePath, header->SceneName, header->ProcessId,
                                   std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::clock_cast<std::chrono::system_clock>(modified).time_since_epoch())
                                       .count()});
            }
            std::ranges::sort(entries, std::greater{}, &RecoveryEntry::Modified);
            return entries;
        }

        [[nodiscard]] std::optional<RecoveryHeader> ReadHeader(const std::filesystem::path& file) const
        {
            std::ifstream in(file, std::ios::binary);
            if (!in) return std::nullopt;
            std::string head(1024, '\0');
            in.read(head.data(), static_cast<std::streamsize>(head.size()));
            head.resize(static_cast<std::size_t>(in.gcount()));
            return ParseRecoveryHeader(head);
        }

        static void Remove(const std::filesystem::path& file)
        {
            std::error_code ec;
            std::filesystem::remove(file, ec);
        }

    private:
        std::filesystem::path m_Directory;
        long m_ProcessId = 0;
        AliveCheck m_Alive;
        mutable std::mutex m_Mutex;
        std::filesystem::path m_Last;
    };

    struct RecoveredScene
    {
        std::filesystem::path File;
        std::string ScenePath;
        std::string SceneName;
    };
}
