module;
#include <yaml-cpp/yaml.h>
export module MoleHole:Application.CameraBookmarks;

import std;
import glm;
import GPP;

export namespace MoleHole
{
    inline constexpr const char* kCameraBookmarksKey = "EditorBookmarks";
    inline constexpr std::size_t kBookmarkSlots = 9;

    struct CameraBookmark
    {
        glm::dvec3 PositionM{0.0};
        float Yaw = 0.0f;
        float Pitch = 0.0f;
        float Fov = 60.0f;
        double SpeedMs = 0.0;

        [[nodiscard]] bool operator==(const CameraBookmark&) const = default;
    };

    class CameraBookmarks
    {
    public:
        [[nodiscard]] bool Valid(const std::size_t slot) const noexcept { return slot < kBookmarkSlots; }

        bool Set(const std::size_t slot, const CameraBookmark& bookmark)
        {
            if (!Valid(slot)) return false;
            m_Slots[slot] = bookmark;
            return true;
        }

        [[nodiscard]] std::optional<CameraBookmark> Get(const std::size_t slot) const
        {
            return Valid(slot) ? m_Slots[slot] : std::nullopt;
        }

        void Clear(const std::size_t slot)
        {
            if (Valid(slot)) m_Slots[slot].reset();
        }

        [[nodiscard]] std::size_t Count() const noexcept
        {
            return static_cast<std::size_t>(std::ranges::count_if(m_Slots, [](const auto& slot) { return slot.has_value(); }));
        }

        [[nodiscard]] bool operator==(const CameraBookmarks&) const = default;

        [[nodiscard]] std::string ToYaml() const
        {
            if (Count() == 0) return {};
            YAML::Node list(YAML::NodeType::Sequence);
            for (std::size_t slot = 0; slot < kBookmarkSlots; ++slot)
            {
                if (!m_Slots[slot]) continue;
                const auto& bookmark = *m_Slots[slot];
                YAML::Node node;
                node["Slot"] = slot + 1;
                YAML::Node position(YAML::NodeType::Sequence);
                position.SetStyle(YAML::EmitterStyle::Flow);
                for (int i = 0; i < 3; ++i) position.push_back(bookmark.PositionM[i]);
                node["PositionM"] = position;
                node["Yaw"] = bookmark.Yaw;
                node["Pitch"] = bookmark.Pitch;
                node["Fov"] = bookmark.Fov;
                node["SpeedMs"] = bookmark.SpeedMs;
                list.push_back(node);
            }
            return YAML::Dump(list);
        }

        [[nodiscard]] static CameraBookmarks FromNode(const YAML::Node& list)
        {
            CameraBookmarks result;
            if (!list.IsSequence()) return result;
            for (const auto& node : list)
            {
                try
                {
                    if (const auto read = ReadEntry(node)) result.m_Slots[read->first] = read->second;
                }
                catch (const YAML::Exception&)
                {
                }
            }
            return result;
        }

        void WriteTo(GPP::Scene& scene) const
        {
            const auto text = ToYaml();
            if (text.empty()) scene.RemoveExtension(kCameraBookmarksKey);
            else scene.SetExtension(kCameraBookmarksKey, YAML::Load(text));
        }

        [[nodiscard]] std::uint32_t Mask() const noexcept
        {
            std::uint32_t mask = 0;
            for (std::size_t slot = 0; slot < kBookmarkSlots; ++slot)
                if (m_Slots[slot]) mask |= 1u << slot;
            return mask;
        }

        [[nodiscard]] static CameraBookmarks FromScene(const GPP::Scene& scene)
        {
            const auto* node = scene.FindExtension(kCameraBookmarksKey);
            return node ? FromNode(*node) : CameraBookmarks{};
        }

    private:
        static std::optional<std::pair<std::size_t, CameraBookmark>> ReadEntry(const YAML::Node& node)
        {
            if (!node.IsMap() || !node["Slot"] || !node["PositionM"]) return std::nullopt;
            const auto number = node["Slot"].as<std::size_t>();
            const auto position = node["PositionM"];
            if (number < 1 || number > kBookmarkSlots || !position.IsSequence() || position.size() != 3) return std::nullopt;
            CameraBookmark bookmark;
            for (int i = 0; i < 3; ++i)
            {
                bookmark.PositionM[i] = position[i].as<double>();
                if (!std::isfinite(bookmark.PositionM[i])) return std::nullopt;
            }
            bookmark.Yaw = node["Yaw"].as<float>(bookmark.Yaw);
            bookmark.Pitch = node["Pitch"].as<float>(bookmark.Pitch);
            bookmark.Fov = node["Fov"].as<float>(bookmark.Fov);
            bookmark.SpeedMs = node["SpeedMs"].as<double>(bookmark.SpeedMs);
            if (!std::isfinite(bookmark.Yaw) || !std::isfinite(bookmark.Pitch) || !std::isfinite(bookmark.Fov) ||
                !std::isfinite(bookmark.SpeedMs))
                return std::nullopt;
            return std::pair{number - 1, bookmark};
        }

        std::array<std::optional<CameraBookmark>, kBookmarkSlots> m_Slots;
    };
}
