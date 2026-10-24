export module MoleHole:UI.LayoutLogic;

import std;
import :UI.State;

export namespace MoleHole
{
    struct LayoutWindow
    {
        std::string_view Title;
        bool UiState::*Flag;
    };

    inline constexpr std::array kLayoutWindows{
        LayoutWindow{"Scene", &UiState::ShowSceneWindow},
        LayoutWindow{"Camera", &UiState::ShowCameraWindow},
        LayoutWindow{"System", &UiState::ShowSystemWindow},
        LayoutWindow{"Profiler", &UiState::ShowProfilerWindow},
        LayoutWindow{"Log", &UiState::ShowLogWindow},
        LayoutWindow{"Undo History", &UiState::ShowUndoHistoryWindow},
        LayoutWindow{"Render", &UiState::ShowRenderWindow},
        LayoutWindow{"Simulation", &UiState::ShowSimulationWindow},
        LayoutWindow{"General Relativity", &UiState::ShowGeneralRelativityWindow},
        LayoutWindow{"Science", &UiState::ShowScienceWindow},
        LayoutWindow{"Animation Graph", &UiState::ShowAnimationGraphWindow},
    };

    enum class LayoutKind { Edit, Render, Graph };

    struct BuiltInLayout
    {
        std::string_view Name;
        LayoutKind Kind;
        std::vector<std::string_view> Windows;
    };

    [[nodiscard]] inline const std::vector<BuiltInLayout>& BuiltInLayouts()
    {
        static const std::vector<BuiltInLayout> layouts{
            {"Edit", LayoutKind::Edit, {"Scene", "Camera", "System", "Simulation", "Log", "Undo History"}},
            {"Render", LayoutKind::Render, {"Render", "Camera", "System", "General Relativity", "Simulation"}},
            {"Graph", LayoutKind::Graph, {"Scene", "Animation Graph", "Simulation", "Log"}},
        };
        return layouts;
    }

    [[nodiscard]] inline const BuiltInLayout* FindBuiltInLayout(const std::string_view name)
    {
        const auto& layouts = BuiltInLayouts();
        const auto found = std::ranges::find(layouts, name, &BuiltInLayout::Name);
        return found == layouts.end() ? nullptr : &*found;
    }

    inline constexpr std::string_view kLayoutSection = "[MoleHole][Layout]";
    inline constexpr std::string_view kLayoutVisibleKey = "Visible=";
    inline constexpr std::size_t kMaxLayoutNameLength = 48;

    [[nodiscard]] inline std::optional<std::string> SanitizeLayoutName(const std::string_view raw)
    {
        std::string name;
        for (const char c : raw)
        {
            const auto u = static_cast<unsigned char>(c);
            if (std::isalnum(u) || c == '-' || c == '_' || c == ' ' || c == '(' || c == ')') name += c;
            else if (u >= 0x80) name += c;
            else name += '_';
        }
        const auto first = name.find_first_not_of(' ');
        if (first == std::string::npos) return std::nullopt;
        name = name.substr(first, name.find_last_not_of(' ') - first + 1);
        if (name.size() > kMaxLayoutNameLength) name.resize(kMaxLayoutNameLength);
        return name;
    }

    [[nodiscard]] inline bool IsReservedLayoutName(const std::string_view name)
    {
        return std::ranges::any_of(BuiltInLayouts(), [&](const BuiltInLayout& layout)
        {
            return std::ranges::equal(layout.Name, name, [](const char a, const char b)
                                      { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
        });
    }

    [[nodiscard]] inline std::filesystem::path LayoutPath(const std::filesystem::path& directory, const std::string_view name)
    {
        return directory / (std::string(name) + ".ini");
    }

    [[nodiscard]] inline std::vector<std::string> ListUserLayouts(const std::filesystem::path& directory)
    {
        std::vector<std::string> names;
        std::error_code ec;
        if (!std::filesystem::exists(directory, ec)) return names;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        {
            if (ec) break;
            if (!entry.is_regular_file() || entry.path().extension() != ".ini") continue;
            auto stem = entry.path().stem().string();
            if (!IsReservedLayoutName(stem)) names.push_back(std::move(stem));
        }
        std::ranges::sort(names);
        return names;
    }

    [[nodiscard]] inline std::vector<std::string> CollectVisibleWindows(const UiState& ui)
    {
        std::vector<std::string> titles;
        for (const auto& window : kLayoutWindows)
        {
            if (ui.*window.Flag) titles.emplace_back(window.Title);
        }
        return titles;
    }

    inline void ApplyVisibleWindows(UiState& ui, const std::span<const std::string> titles)
    {
        for (const auto& window : kLayoutWindows)
        {
            ui.*window.Flag = std::ranges::contains(titles, window.Title);
        }
    }

    inline void ApplyVisibleWindows(UiState& ui, const std::span<const std::string_view> titles)
    {
        for (const auto& window : kLayoutWindows)
        {
            ui.*window.Flag = std::ranges::contains(titles, window.Title);
        }
    }

    [[nodiscard]] inline std::string StripLayoutSection(const std::string_view ini)
    {
        std::string result;
        bool skipping = false;
        std::size_t begin = 0;
        while (begin < ini.size())
        {
            auto end = ini.find('\n', begin);
            if (end == std::string_view::npos) end = ini.size();
            const auto line = ini.substr(begin, end - begin);
            if (!line.empty() && line.front() == '[') skipping = line.starts_with(kLayoutSection);
            if (!skipping)
            {
                result.append(line);
                if (end < ini.size()) result += '\n';
            }
            begin = end + 1;
        }
        return result;
    }

    [[nodiscard]] inline std::string EncodeLayout(const std::string_view ini, const std::span<const std::string> visible)
    {
        auto text = StripLayoutSection(ini);
        if (!text.empty() && text.back() != '\n') text += '\n';
        if (!text.empty() && !text.ends_with("\n\n")) text += '\n';
        text += kLayoutSection;
        text += '\n';
        text += kLayoutVisibleKey;
        for (std::size_t i = 0; i < visible.size(); ++i)
        {
            if (i) text += ',';
            text += visible[i];
        }
        text += "\n\n";
        return text;
    }

    [[nodiscard]] inline std::optional<std::vector<std::string>> DecodeLayoutVisible(const std::string_view ini)
    {
        bool inside = false;
        std::size_t begin = 0;
        while (begin < ini.size())
        {
            auto end = ini.find('\n', begin);
            if (end == std::string_view::npos) end = ini.size();
            auto line = ini.substr(begin, end - begin);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (!line.empty() && line.front() == '[') inside = line.starts_with(kLayoutSection);
            else if (inside && line.starts_with(kLayoutVisibleKey))
            {
                std::vector<std::string> titles;
                auto rest = line.substr(kLayoutVisibleKey.size());
                while (!rest.empty())
                {
                    const auto comma = rest.find(',');
                    const auto token = rest.substr(0, comma);
                    if (!token.empty()) titles.emplace_back(token);
                    if (comma == std::string_view::npos) break;
                    rest.remove_prefix(comma + 1);
                }
                return titles;
            }
            begin = end + 1;
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::vector<std::string> LayoutChoices(const std::filesystem::path& directory)
    {
        std::vector<std::string> names;
        for (const auto& layout : BuiltInLayouts()) names.emplace_back(layout.Name);
        auto user = ListUserLayouts(directory);
        names.insert(names.end(), std::make_move_iterator(user.begin()), std::make_move_iterator(user.end()));
        return names;
    }
}
