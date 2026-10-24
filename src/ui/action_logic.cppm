export module MoleHole:UI.ActionLogic;

import std;
import :UI.FuzzySearch;

export namespace MoleHole
{
    enum class ActionNeed : std::uint32_t
    {
        None = 0,
        Scene = 1u << 0,
        Selection = 1u << 1,
        Paused = 1u << 2,
        CanUndo = 1u << 3,
        CanRedo = 1u << 4,
    };

    [[nodiscard]] constexpr ActionNeed operator|(const ActionNeed a, const ActionNeed b) noexcept
    {
        return static_cast<ActionNeed>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }

    [[nodiscard]] constexpr bool HasNeed(const ActionNeed set, const ActionNeed need) noexcept
    {
        return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(need)) != 0;
    }

    struct ActionContext
    {
        bool HasScene = false;
        bool HasSelection = false;
        bool Paused = true;
        bool CanUndo = false;
        bool CanRedo = false;
    };

    [[nodiscard]] inline std::string DisabledReason(const ActionNeed needs, const ActionContext& context)
    {
        if (HasNeed(needs, ActionNeed::Scene) && !context.HasScene) return "No scene loaded";
        if (HasNeed(needs, ActionNeed::Selection) && !context.HasSelection) return "No entity selected";
        if (HasNeed(needs, ActionNeed::Paused) && !context.Paused) return "Pause the simulation first";
        if (HasNeed(needs, ActionNeed::CanUndo) && !context.CanUndo) return "Nothing to undo";
        if (HasNeed(needs, ActionNeed::CanRedo) && !context.CanRedo) return "Nothing to redo";
        return {};
    }

    struct ActionDescriptor
    {
        std::string Id;
        std::string Label;
        std::string Category;
        std::string Shortcut;
        std::string Scope;
        std::string Keywords;
        ActionNeed Needs = ActionNeed::None;
        const bool* Toggle = nullptr;
        bool Repeat = false;
        bool OwnGuard = false;
        std::function<void()> Invoke;
    };

    inline constexpr auto kBindableKeys = std::to_array<std::string_view>({
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V",
        "W", "X", "Y", "Z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9",
        "F10", "F11", "F12", "Del", "Enter", "Backspace", "Tab", "Space", ".", ",", "[", "]", "-", "=", ";", "'", "`", "\\", "Up",
        "Down", "Left", "Right", "Home", "End", "PageUp", "PageDown", "Insert", "Num +", "Num -",
    });

    inline constexpr auto kReservedBareKeys = std::to_array<std::string_view>({
        "Enter", "Tab", "Space", "Backspace", "Up", "Down", "Left", "Right", "W", "A", "S", "D", "Q", "E",
    });

    namespace ShortcutDetail
    {
        inline std::string Trim(std::string_view text)
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
            return std::string(text);
        }

        inline std::string Lower(std::string_view text)
        {
            std::string result(text);
            std::ranges::transform(result, result.begin(), FuzzyDetail::Lower);
            return result;
        }

        inline std::string KeyName(const std::string_view key)
        {
            const auto lower = Lower(key);
            if (lower == "del" || lower == "delete") return "Del";
            if (lower == "esc" || lower == "escape") return "Esc";
            if (lower == "enter" || lower == "return") return "Enter";
            if (lower == "backspace") return "Backspace";
            if (lower == "tab") return "Tab";
            if (lower == "space") return "Space";
            if (const auto known = std::ranges::find_if(kBindableKeys, [&](const std::string_view name) { return Lower(name) == lower; });
                known != kBindableKeys.end() && key.size() > 1)
                return std::string(*known);
            if (lower.size() >= 2 && lower.front() == 'f' && std::ranges::all_of(lower.substr(1), [](const char c)
                                                                                  { return std::isdigit(static_cast<unsigned char>(c)); }))
                return "F" + lower.substr(1);
            if (key.size() == 1) return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(key.front()))));
            return std::string(key);
        }
    }

    [[nodiscard]] inline std::string NormalizeShortcut(const std::string_view chord)
    {
        std::string rest = ShortcutDetail::Trim(chord);
        bool ctrl = false;
        bool alt = false;
        bool shift = false;
        for (bool again = true; again;)
        {
            again = false;
            const auto plus = rest.find('+');
            if (plus == std::string::npos || plus == 0) break;
            const auto word = ShortcutDetail::Lower(ShortcutDetail::Trim(std::string_view(rest).substr(0, plus)));
            bool* flag = word == "ctrl" || word == "control" ? &ctrl : word == "alt" ? &alt : word == "shift" ? &shift : nullptr;
            if (!flag) break;
            *flag = true;
            rest = ShortcutDetail::Trim(std::string_view(rest).substr(plus + 1));
            again = true;
        }
        if (rest.empty()) return {};
        std::string result;
        if (ctrl) result += "Ctrl+";
        if (alt) result += "Alt+";
        if (shift) result += "Shift+";
        return result + ShortcutDetail::KeyName(rest);
    }

    [[nodiscard]] inline std::vector<std::string> SplitShortcuts(const std::string_view text)
    {
        std::vector<std::string> chords;
        std::size_t begin = 0;
        while (begin <= text.size())
        {
            auto end = text.find('/', begin);
            if (end == std::string_view::npos) end = text.size();
            if (auto chord = NormalizeShortcut(text.substr(begin, end - begin)); !chord.empty()) chords.push_back(std::move(chord));
            begin = end + 1;
        }
        return chords;
    }

    [[nodiscard]] inline std::string FormatShortcuts(const std::string_view text)
    {
        std::string result;
        for (const auto& chord : SplitShortcuts(text))
        {
            if (!result.empty()) result += " / ";
            result += chord;
        }
        return result;
    }

    [[nodiscard]] inline std::string PrimaryShortcut(const std::string_view text)
    {
        const auto chords = SplitShortcuts(text);
        return chords.empty() ? std::string() : chords.front();
    }

    struct ShortcutConflict
    {
        std::string First;
        std::string Second;
        std::string Chord;
    };

    [[nodiscard]] inline std::vector<ShortcutConflict> FindShortcutConflicts(const std::span<const ActionDescriptor> actions)
    {
        std::vector<ShortcutConflict> conflicts;
        for (std::size_t a = 0; a < actions.size(); ++a)
        {
            const auto first = SplitShortcuts(actions[a].Shortcut);
            for (std::size_t b = a + 1; b < actions.size(); ++b)
            {
                if (actions[a].Scope != actions[b].Scope) continue;
                for (const auto& chord : SplitShortcuts(actions[b].Shortcut))
                {
                    if (std::ranges::contains(first, chord)) conflicts.push_back({actions[a].Id, actions[b].Id, chord});
                }
            }
        }
        return conflicts;
    }

    struct Chord
    {
        bool Ctrl = false;
        bool Alt = false;
        bool Shift = false;
        std::string Key;
    };

    [[nodiscard]] inline Chord ParseChord(const std::string_view text)
    {
        const auto normalized = NormalizeShortcut(text);
        Chord chord;
        std::string_view rest = normalized;
        for (bool again = true; again;)
        {
            again = false;
            for (const auto& [prefix, flag] : {std::pair<std::string_view, bool Chord::*>{"Ctrl+", &Chord::Ctrl},
                                               {"Alt+", &Chord::Alt}, {"Shift+", &Chord::Shift}})
            {
                if (rest.starts_with(prefix) && rest.size() > prefix.size())
                {
                    chord.*flag = true;
                    rest.remove_prefix(prefix.size());
                    again = true;
                }
            }
        }
        chord.Key = std::string(rest);
        return chord;
    }

    [[nodiscard]] inline std::string ChordProblem(const std::string_view text)
    {
        if (text.find('/') != std::string_view::npos) return "One key combination only";
        const auto chord = ParseChord(text);
        if (chord.Key.empty()) return "No key pressed";
        if (!std::ranges::contains(kBindableKeys, std::string_view(chord.Key))) return "This key cannot be bound";
        if (!chord.Ctrl && !chord.Alt && std::ranges::contains(kReservedBareKeys, std::string_view(chord.Key)))
            return "Reserved for navigation and camera movement without Ctrl or Alt";
        return {};
    }

    [[nodiscard]] inline bool IsRebindable(const ActionDescriptor& action)
    {
        return action.Scope.empty() && action.Id.find(':') == std::string::npos && static_cast<bool>(action.Invoke);
    }

    using ShortcutOverrides = std::map<std::string, std::string>;

    inline void ApplyShortcutOverrides(const std::span<ActionDescriptor> actions, const ShortcutOverrides& overrides)
    {
        for (auto& action : actions)
        {
            if (!IsRebindable(action)) continue;
            if (const auto found = overrides.find(action.Id); found != overrides.end()) action.Shortcut = found->second;
        }
    }

    [[nodiscard]] inline const ActionDescriptor* FindAction(const std::span<const ActionDescriptor> actions, const std::string_view id)
    {
        const auto found = std::ranges::find(actions, id, &ActionDescriptor::Id);
        return found == actions.end() ? nullptr : &*found;
    }

    [[nodiscard]] inline std::vector<const ActionDescriptor*> FindChordHolders(const std::span<const ActionDescriptor> actions,
                                                                             const std::string_view excludeId,
                                                                             const std::string_view chord)
    {
        std::vector<const ActionDescriptor*> holders;
        const auto wanted = NormalizeShortcut(chord);
        for (const auto& action : actions)
        {
            if (action.Id == excludeId || action.Scope == "Graph Editor") continue;
            if (std::ranges::contains(SplitShortcuts(action.Shortcut), wanted)) holders.push_back(&action);
        }
        return holders;
    }

    struct RebindOutcome
    {
        bool Applied = false;
        std::string Problem;
        std::vector<std::string> Holders;
    };

    [[nodiscard]] inline RebindOutcome TrySetShortcut(ShortcutOverrides& overrides, const std::span<const ActionDescriptor> defaults,
                                                      const std::string_view id, const std::string_view chordText)
    {
        RebindOutcome outcome;
        const auto* base = FindAction(defaults, id);
        if (!base || !IsRebindable(*base))
        {
            outcome.Problem = "This action cannot be rebound";
            return outcome;
        }
        const auto chord = NormalizeShortcut(chordText);
        if (outcome.Problem = ChordProblem(chord); !outcome.Problem.empty()) return outcome;

        auto effective = std::vector<ActionDescriptor>(defaults.begin(), defaults.end());
        ApplyShortcutOverrides(effective, overrides);
        for (const auto* holder : FindChordHolders(effective, id, chord))
            outcome.Holders.push_back(holder->Label + (holder->Scope.empty() ? std::string() : " (" + holder->Scope + ")"));
        if (!outcome.Holders.empty()) return outcome;

        if (chord == FormatShortcuts(base->Shortcut)) overrides.erase(std::string(id));
        else overrides[std::string(id)] = chord;
        outcome.Applied = true;
        return outcome;
    }

    inline void ClearShortcut(ShortcutOverrides& overrides, const std::span<const ActionDescriptor> defaults, const std::string_view id)
    {
        const auto* base = FindAction(defaults, id);
        if (!base || !IsRebindable(*base)) return;
        if (base->Shortcut.empty()) overrides.erase(std::string(id));
        else overrides[std::string(id)] = {};
    }

    inline void ResetShortcut(ShortcutOverrides& overrides, const std::string_view id) { overrides.erase(std::string(id)); }

    [[nodiscard]] inline ShortcutOverrides SanitizeShortcutOverrides(const ShortcutOverrides& raw,
                                                                    const std::span<const ActionDescriptor> defaults)
    {
        ShortcutOverrides clean;
        for (const auto& [id, text] : raw)
        {
            if (text.empty())
            {
                ClearShortcut(clean, defaults, id);
                continue;
            }
            (void)TrySetShortcut(clean, defaults, id, text);
        }
        return clean;
    }

    class ShortcutOverrideStore
    {
    public:
        [[nodiscard]] static ShortcutOverrideStore& Instance()
        {
            static ShortcutOverrideStore store;
            return store;
        }

        [[nodiscard]] const ShortcutOverrides& Overrides() const noexcept { return m_Overrides; }
        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Revision; }

        void Set(ShortcutOverrides overrides)
        {
            if (overrides == m_Overrides) return;
            m_Overrides = std::move(overrides);
            ++m_Revision;
        }

    private:
        ShortcutOverrides m_Overrides;
        std::uint64_t m_Revision = 0;
    };

    class RecentActions
    {
    public:
        static constexpr std::size_t kCapacity = 8;

        void Touch(const std::string_view id)
        {
            std::erase(m_Ids, id);
            m_Ids.insert(m_Ids.begin(), std::string(id));
            if (m_Ids.size() > kCapacity) m_Ids.resize(kCapacity);
        }

        [[nodiscard]] const std::vector<std::string>& Ids() const noexcept { return m_Ids; }

    private:
        std::vector<std::string> m_Ids;
    };

    struct ActionRequest
    {
        std::string Id;
        std::string Arg;
    };

    class ActionRequests
    {
    public:
        void Push(ActionRequest request)
        {
            std::scoped_lock lock(m_Mutex);
            m_Pending.push_back(std::move(request));
        }

        [[nodiscard]] std::vector<ActionRequest> Take(const std::initializer_list<std::string_view> prefixes)
        {
            std::scoped_lock lock(m_Mutex);
            std::vector<ActionRequest> taken;
            std::erase_if(m_Pending, [&](ActionRequest& request)
            {
                const bool mine = std::ranges::any_of(prefixes, [&](const std::string_view prefix)
                                                      { return std::string_view(request.Id).starts_with(prefix); });
                if (mine) taken.push_back(std::move(request));
                return mine;
            });
            return taken;
        }

    private:
        std::mutex m_Mutex;
        std::vector<ActionRequest> m_Pending;
    };

    [[nodiscard]] inline std::optional<int> SubstringScore(const std::string_view query, const std::string_view target)
    {
        const auto text = ShortcutDetail::Lower(target);
        int total = 0;
        std::size_t i = 0;
        bool any = false;
        while (i < query.size())
        {
            while (i < query.size() && std::isspace(static_cast<unsigned char>(query[i]))) ++i;
            const auto begin = i;
            while (i < query.size() && !std::isspace(static_cast<unsigned char>(query[i]))) ++i;
            if (begin == i) break;
            const auto token = ShortcutDetail::Lower(query.substr(begin, i - begin));
            const auto found = text.find(token);
            if (found == std::string::npos) return std::nullopt;
            total += 10 + static_cast<int>(token.size()) - static_cast<int>(std::min<std::size_t>(found, 8));
            any = true;
        }
        return any ? std::optional<int>(total) : std::optional<int>(0);
    }

    struct RankedAction
    {
        std::size_t Index = 0;
        int Score = 0;
        std::string Reason;
        [[nodiscard]] bool Enabled() const noexcept { return Reason.empty(); }
    };

    [[nodiscard]] inline std::vector<RankedAction> RankActions(const std::string_view query,
                                                              const std::span<const ActionDescriptor> actions,
                                                              const ActionContext& context,
                                                              const std::span<const std::string> recent)
    {
        const bool empty = query.find_first_not_of(" \t") == std::string_view::npos;
        std::vector<RankedAction> ranked;
        for (std::size_t i = 0; i < actions.size(); ++i)
        {
            const auto& action = actions[i];
            if (!action.Invoke) continue;

            int score = 0;
            if (!empty)
            {
                auto match = FuzzyScore(query, action.Label);
                if (!match)
                {
                    const auto wide = SubstringScore(query, action.Category + " " + action.Label + " " + action.Keywords);
                    if (!wide) continue;
                    match = *wide;
                }
                score = *match;
            }
            if (const auto at = std::ranges::find(recent, action.Id); at != recent.end())
            {
                const int age = static_cast<int>(at - recent.begin());
                score += empty ? 1000 - age : std::max(0, 40 - 4 * age);
            }
            ranked.push_back({i, score, DisabledReason(action.Needs, context)});
        }
        std::ranges::stable_sort(ranked, [](const RankedAction& a, const RankedAction& b)
        {
            if (a.Enabled() != b.Enabled()) return a.Enabled();
            return a.Score > b.Score;
        });
        return ranked;
    }

    struct SheetRow
    {
        std::string Label;
        std::string Shortcut;
        std::string Scope;
    };

    struct SheetGroup
    {
        std::string Category;
        std::vector<SheetRow> Rows;
    };

    [[nodiscard]] inline std::vector<SheetGroup> BuildShortcutSheet(const std::span<const ActionDescriptor> actions,
                                                                   const std::string_view filter)
    {
        std::vector<SheetGroup> groups;
        for (const auto& action : actions)
        {
            const auto shortcut = FormatShortcuts(action.Shortcut);
            if (shortcut.empty()) continue;
            if (!FuzzyScore(filter, action.Category + " " + action.Label + " " + shortcut + " " + action.Scope)) continue;
            auto group = std::ranges::find(groups, action.Category, &SheetGroup::Category);
            if (group == groups.end())
            {
                groups.push_back({action.Category, {}});
                group = std::prev(groups.end());
            }
            group->Rows.push_back({action.Label, shortcut, action.Scope});
        }
        return groups;
    }
}
