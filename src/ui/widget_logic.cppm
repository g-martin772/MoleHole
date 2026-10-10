export module MoleHole:UI.WidgetLogic;

import std;
import glm;

export namespace MoleHole
{
    [[nodiscard]] constexpr float Saturate(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

    [[nodiscard]] constexpr float EaseOutCubic(float t)
    {
        const float u = 1.0f - Saturate(t);
        return 1.0f - u * u * u;
    }

    [[nodiscard]] constexpr float EaseInOutCubic(float t)
    {
        t = Saturate(t);
        if (t < 0.5f) return 4.0f * t * t * t;
        const float u = -2.0f * t + 2.0f;
        return 1.0f - u * u * u / 2.0f;
    }

    // Frame-rate-independent exponential smoothing: the same wall-clock time yields the same result at any dt.
    [[nodiscard]] inline float ApproachExp(float current, float target, float rate, float dt)
    {
        if (dt <= 0.0f) return current;
        return target + (current - target) * std::exp(-rate * dt);
    }

    [[nodiscard]] inline float ApproachLinear(float current, float target, float speed, float dt)
    {
        const float step = speed * dt;
        if (current < target) return std::min(current + step, target);
        return std::max(current - step, target);
    }

    [[nodiscard]] inline glm::vec4 LerpColor(const glm::vec4& a, const glm::vec4& b, float t)
    {
        return a + (b - a) * Saturate(t);
    }

    [[nodiscard]] inline glm::vec4 Lighten(const glm::vec4& c, float amount)
    {
        return {glm::mix(glm::vec3(c), glm::vec3(1.0f), Saturate(amount)), c.a};
    }

    [[nodiscard]] inline glm::vec4 Darken(const glm::vec4& c, float amount)
    {
        return {glm::mix(glm::vec3(c), glm::vec3(0.0f), Saturate(amount)), c.a};
    }

    [[nodiscard]] inline glm::vec4 WithAlpha(const glm::vec4& c, float alpha) { return {c.r, c.g, c.b, alpha}; }

    [[nodiscard]] inline float Luminance(const glm::vec4& c)
    {
        return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    }

    [[nodiscard]] inline glm::vec4 HexColor(std::uint32_t rgb, float alpha = 1.0f)
    {
        return {static_cast<float>((rgb >> 16) & 0xff) / 255.0f, static_cast<float>((rgb >> 8) & 0xff) / 255.0f,
                static_cast<float>(rgb & 0xff) / 255.0f, alpha};
    }

    // Eased expand/collapse progress: Progress eases toward the open/closed target at a fixed duration.
    struct AnimatedToggle
    {
        float Linear = 0.0f;

        void Snap(bool open) { Linear = open ? 1.0f : 0.0f; }

        void Update(bool open, float dt, float duration = 0.18f)
        {
            Linear = ApproachLinear(Linear, open ? 1.0f : 0.0f, duration > 0.0f ? 1.0f / duration : 1000.0f, dt);
        }

        [[nodiscard]] float Eased() const { return EaseInOutCubic(Linear); }
        [[nodiscard]] bool Settled(bool open) const { return Linear == (open ? 1.0f : 0.0f); }
    };

    [[nodiscard]] inline std::string SpacedName(const std::string& name)
    {
        std::string result;
        for (std::size_t i = 0; i < name.size(); ++i)
        {
            if (i > 0 && std::isupper(static_cast<unsigned char>(name[i])) &&
                !std::isupper(static_cast<unsigned char>(name[i - 1])))
            {
                result += ' ';
            }
            result += name[i];
        }
        return result;
    }

    [[nodiscard]] inline bool ContainsInsensitive(std::string_view haystack, std::string_view needle)
    {
        if (needle.empty()) return true;
        const auto it = std::ranges::search(haystack, needle, [](char a, char b)
        {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
        return !it.empty();
    }

    [[nodiscard]] inline std::string Utf8Encode(std::uint32_t cp)
    {
        std::string out;
        if (cp < 0x80)
        {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        return out;
    }

    // Font Awesome solid codepoints used as entity type icons.
    [[nodiscard]] inline std::uint32_t IconCodepointForType(std::string_view typeTag)
    {
        if (typeTag == "BlackHole") return 0xf111;
        if (typeTag == "Sphere") return 0xf0ac;
        if (typeTag == "Mesh") return 0xf1b2;
        if (typeTag == "Camera") return 0xf03d;
        return 0xf1b3;
    }

    struct OutlinerEntry
    {
        std::uint64_t Guid = 0;
        std::uint64_t Parent = 0;
        std::string Name;
        std::string Type;
    };

    struct OutlinerRow
    {
        std::uint64_t Guid = 0;
        int Depth = 0;
        bool HasChildren = false;
        bool Expanded = false;
    };

    // Flattens the entity hierarchy into visible rows. With a filter, only matches and their ancestors remain and
    // every shown ancestor is expanded. Entities whose parent is missing (or in a cycle) are treated as roots.
    [[nodiscard]] inline std::vector<OutlinerRow> BuildOutlinerRows(
        const std::vector<OutlinerEntry>& entries, const std::unordered_set<std::uint64_t>& expanded,
        std::string_view filter)
    {
        std::unordered_map<std::uint64_t, std::size_t> index;
        for (std::size_t i = 0; i < entries.size(); ++i) index.emplace(entries[i].Guid, i);

        std::vector<std::vector<std::size_t>> children(entries.size());
        std::vector<std::size_t> roots;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const auto parent = index.find(entries[i].Parent);
            if (entries[i].Parent == 0 || parent == index.end() || parent->second == i) roots.push_back(i);
            else children[parent->second].push_back(i);
        }

        std::vector<char> visited(entries.size(), 0);
        std::vector<char> shown(entries.size(), 1);
        const bool filtering = !filter.empty();
        std::function<bool(std::size_t)> mark = [&](std::size_t i)
        {
            if (visited[i]) return false;
            visited[i] = 1;
            bool any = ContainsInsensitive(entries[i].Name, filter) || ContainsInsensitive(entries[i].Type, filter);
            for (const auto child : children[i]) any = mark(child) || any;
            shown[i] = any;
            return any;
        };

        std::vector<OutlinerRow> rows;
        std::vector<char> emitted(entries.size(), 0);
        std::function<void(std::size_t, int)> emit = [&](std::size_t i, int depth)
        {
            if (emitted[i] || !shown[i]) return;
            emitted[i] = 1;
            bool hasChildren = false;
            for (const auto child : children[i]) hasChildren = hasChildren || shown[child];
            const bool open = hasChildren && (filtering || expanded.contains(entries[i].Guid));
            rows.push_back({entries[i].Guid, depth, hasChildren, open});
            if (!open) return;
            for (const auto child : children[i]) emit(child, depth + 1);
        };

        if (filtering)
        {
            std::ranges::fill(shown, 0);
            for (const auto root : roots) mark(root);
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                if (!visited[i]) mark(i);
            }
        }

        std::vector<char> reachable(entries.size(), 0);
        std::function<void(std::size_t)> reach = [&](std::size_t i)
        {
            if (reachable[i]) return;
            reachable[i] = 1;
            for (const auto child : children[i]) reach(child);
        };
        for (const auto root : roots) reach(root);

        for (const auto root : roots) emit(root, 0);
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (!reachable[i]) emit(i, 0);
        }
        return rows;
    }

    [[nodiscard]] inline bool CanReparent(const std::vector<OutlinerEntry>& entries, std::uint64_t guid,
                                          std::uint64_t newParent)
    {
        if (guid == 0 || guid == newParent) return false;
        if (newParent == 0) return true;
        std::unordered_map<std::uint64_t, std::uint64_t> parentOf;
        for (const auto& entry : entries) parentOf.emplace(entry.Guid, entry.Parent);
        if (!parentOf.contains(newParent) || !parentOf.contains(guid)) return false;
        std::uint64_t cursor = newParent;
        for (std::size_t steps = 0; steps <= entries.size() && cursor != 0; ++steps)
        {
            if (cursor == guid) return false;
            const auto it = parentOf.find(cursor);
            if (it == parentOf.end()) break;
            cursor = it->second;
        }
        return true;
    }

    [[nodiscard]] inline std::vector<std::uint64_t> RowRange(const std::vector<OutlinerRow>& rows,
                                                             std::uint64_t anchor, std::uint64_t target)
    {
        const auto find = [&](std::uint64_t guid)
        {
            return std::ranges::find(rows, guid, &OutlinerRow::Guid);
        };
        const auto a = find(anchor);
        const auto b = find(target);
        std::vector<std::uint64_t> result;
        if (b == rows.end()) return result;
        if (a == rows.end())
        {
            result.push_back(target);
            return result;
        }
        const auto [first, last] = std::minmax(a, b);
        for (auto it = first; it <= last; ++it) result.push_back(it->Guid);
        return result;
    }

    // Children of the given guids that are not themselves being removed, and which would be orphaned.
    [[nodiscard]] inline std::vector<std::uint64_t> OrphanedChildren(const std::vector<OutlinerEntry>& entries,
                                                                    const std::unordered_set<std::uint64_t>& removed)
    {
        std::vector<std::uint64_t> result;
        for (const auto& entry : entries)
        {
            if (!removed.contains(entry.Guid) && removed.contains(entry.Parent)) result.push_back(entry.Guid);
        }
        return result;
    }
}
