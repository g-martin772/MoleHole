module;
#include <yaml-cpp/yaml.h>
export module MoleHole:Application.SceneCameras;

import std;
import glm;
import GPP;

export namespace MoleHole
{
    struct SceneCameraPose
    {
        glm::dvec3 PositionM{0.0};
        float Yaw = 0.0f;
        float Pitch = 0.0f;
        double SpeedMs = 0.0;
        std::int64_t SavedAt = 0;

        [[nodiscard]] bool SamePose(const SceneCameraPose& other) const noexcept
        {
            return PositionM == other.PositionM && Yaw == other.Yaw && Pitch == other.Pitch && SpeedMs == other.SpeedMs;
        }
    };

    class SceneCameraCache
    {
    public:
        static constexpr std::size_t kDefaultCapacity = 500;
        static constexpr std::size_t kPruneChecks = 300;

        explicit SceneCameraCache(std::filesystem::path file = {}, const std::size_t capacity = kDefaultCapacity)
            : m_File(std::move(file)), m_Capacity(std::max<std::size_t>(capacity, 1))
        {
        }

        [[nodiscard]] static std::string KeyFor(const std::filesystem::path& scene)
        {
            if (scene.empty()) return {};
            std::error_code error;
            auto absolute = std::filesystem::absolute(scene, error);
            if (error) return scene.lexically_normal().string();
            auto canonical = std::filesystem::weakly_canonical(absolute, error);
            return (error ? absolute.lexically_normal() : canonical).string();
        }

        std::string Load()
        {
            m_Entries.clear();
            m_Dirty = false;
            std::error_code error;
            if (m_File.empty() || !std::filesystem::exists(m_File, error)) return {};
            try
            {
                const auto root = YAML::LoadFile(m_File.string());
                const auto scenes = root["Scenes"];
                if (!scenes || !scenes.IsSequence()) return root.IsNull() ? std::string{} : "no Scenes list";
                for (const auto& node : scenes)
                {
                    try
                    {
                        if (auto entry = ReadEntry(node)) m_Entries[entry->first] = entry->second;
                    }
                    catch (const YAML::Exception&)
                    {
                    }
                }
                Trim();
            }
            catch (const std::exception& e)
            {
                m_Entries.clear();
                return e.what();
            }
            return {};
        }

        [[nodiscard]] std::expected<void, std::string> Save()
        {
            if (!m_Dirty) return {};
            if (m_File.empty()) return std::unexpected("no cache file");
            std::error_code error;
            if (const auto dir = m_File.parent_path(); !dir.empty()) std::filesystem::create_directories(dir, error);

            YAML::Node root;
            root["Version"] = 1;
            auto scenes = YAML::Node(YAML::NodeType::Sequence);
            for (const auto& [key, pose] : Sorted())
            {
                YAML::Node entry;
                entry["Path"] = key;
                YAML::Node position(YAML::NodeType::Sequence);
                position.SetStyle(YAML::EmitterStyle::Flow);
                for (int i = 0; i < 3; ++i) position.push_back(pose.PositionM[i]);
                entry["PositionM"] = position;
                entry["Yaw"] = pose.Yaw;
                entry["Pitch"] = pose.Pitch;
                entry["SpeedMs"] = pose.SpeedMs;
                entry["SavedAt"] = pose.SavedAt;
                scenes.push_back(entry);
            }
            root["Scenes"] = scenes;
            YAML::Emitter out;
            out << root;
            if (auto written = GPP::WriteFileAtomic(m_File, std::string(out.c_str()) + '\n'); !written) return written;
            m_Dirty = false;
            return {};
        }

        [[nodiscard]] std::optional<SceneCameraPose> Lookup(const std::string& key) const
        {
            if (const auto it = m_Entries.find(key); it != m_Entries.end()) return it->second;
            return std::nullopt;
        }

        void Store(const std::string& key, SceneCameraPose pose, const std::int64_t now)
        {
            if (key.empty()) return;
            pose.SavedAt = now;
            auto [it, inserted] = m_Entries.try_emplace(key, pose);
            if (!inserted)
            {
                if (it->second.SamePose(pose)) return;
                it->second = pose;
            }
            m_Dirty = true;
            Trim();
        }

        bool Carry(const std::string& from, const std::string& to, const std::int64_t now)
        {
            if (from.empty() || to.empty() || from == to) return false;
            const auto source = Lookup(from);
            if (!source) return false;
            Store(to, *source, now);
            return true;
        }

        std::size_t Prune(const std::size_t maxChecks = kPruneChecks)
        {
            std::size_t removed = 0;
            std::size_t checked = 0;
            for (const auto& [key, pose] : Sorted())
            {
                if (checked++ >= maxChecks) break;
                std::error_code error;
                const bool exists = std::filesystem::exists(key, error);
                if (!exists && !error)
                {
                    m_Entries.erase(key);
                    ++removed;
                }
            }
            if (removed > 0) m_Dirty = true;
            return removed;
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_Entries.size(); }
        [[nodiscard]] bool Dirty() const noexcept { return m_Dirty; }
        [[nodiscard]] const std::filesystem::path& File() const noexcept { return m_File; }

    private:
        static std::optional<std::pair<std::string, SceneCameraPose>> ReadEntry(const YAML::Node& node)
        {
            if (!node.IsMap()) return std::nullopt;
            const auto path = node["Path"];
            const auto position = node["PositionM"];
            if (!path || !path.IsScalar() || !position || !position.IsSequence() || position.size() != 3) return std::nullopt;
            SceneCameraPose pose;
            for (int i = 0; i < 3; ++i)
            {
                if (!position[i].IsScalar()) return std::nullopt;
                pose.PositionM[i] = position[i].as<double>();
                if (!std::isfinite(pose.PositionM[i])) return std::nullopt;
            }
            pose.Yaw = node["Yaw"].as<float>(0.0f);
            pose.Pitch = node["Pitch"].as<float>(0.0f);
            pose.SpeedMs = node["SpeedMs"].as<double>(0.0);
            pose.SavedAt = node["SavedAt"].as<std::int64_t>(0);
            if (!std::isfinite(pose.Yaw) || !std::isfinite(pose.Pitch) || !std::isfinite(pose.SpeedMs)) return std::nullopt;
            return std::pair{path.as<std::string>(), pose};
        }

        [[nodiscard]] std::vector<std::pair<std::string, SceneCameraPose>> Sorted() const
        {
            std::vector<std::pair<std::string, SceneCameraPose>> entries(m_Entries.begin(), m_Entries.end());
            std::ranges::sort(entries, [](const auto& a, const auto& b)
                              { return a.second.SavedAt != b.second.SavedAt ? a.second.SavedAt < b.second.SavedAt : a.first < b.first; });
            return entries;
        }

        void Trim()
        {
            if (m_Entries.size() <= m_Capacity) return;
            const auto entries = Sorted();
            for (std::size_t i = 0; m_Entries.size() > m_Capacity && i < entries.size(); ++i) m_Entries.erase(entries[i].first);
            m_Dirty = true;
        }

        std::filesystem::path m_File;
        std::size_t m_Capacity;
        std::unordered_map<std::string, SceneCameraPose> m_Entries;
        bool m_Dirty = false;
    };
}
