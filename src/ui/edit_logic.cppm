export module MoleHole:UI.EditLogic;

import std;
import GPP;

export namespace MoleHole
{
    enum class EditAction { SelectAll, Duplicate, Delete, Rename, FrameSelected, Deselect };

    class EditRequests
    {
    public:
        void Push(const EditAction action)
        {
            std::scoped_lock lock(m_Mutex);
            m_Pending.push_back(action);
        }

        [[nodiscard]] std::vector<EditAction> Take(const std::initializer_list<EditAction> kinds)
        {
            std::scoped_lock lock(m_Mutex);
            std::vector<EditAction> taken;
            std::erase_if(m_Pending, [&](const EditAction action)
            {
                if (!std::ranges::contains(kinds, action)) return false;
                taken.push_back(action);
                return true;
            });
            return taken;
        }

    private:
        std::mutex m_Mutex;
        std::vector<EditAction> m_Pending;
    };

    constexpr std::size_t kMaxMenuLabel = 40;

    [[nodiscard]] inline std::string ShortenLabel(const std::string_view text, const std::size_t limit = kMaxMenuLabel)
    {
        if (text.size() <= limit) return std::string(text);
        std::size_t cut = limit - 3;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
        return std::string(text.substr(0, cut)) + "...";
    }

    struct HistoryMenuItem
    {
        std::string Text;
        std::string Shortcut;
        bool Enabled = false;
        std::string Reason;
    };

    [[nodiscard]] inline HistoryMenuItem MakeHistoryMenuItem(const bool redo, const bool hasScene, const bool playing,
                                                            const bool available, const std::string_view label)
    {
        const std::string verb = redo ? "Redo" : "Undo";
        HistoryMenuItem item;
        item.Text = label.empty() ? verb : verb + " " + ShortenLabel(label);
        item.Shortcut = redo ? "Ctrl+Shift+Z" : "Ctrl+Z";
        if (!hasScene) item.Reason = "No scene loaded";
        else if (playing) item.Reason = std::format("{} is disabled while the simulation is playing", verb);
        else if (!available) item.Reason = std::format("Nothing to {}", redo ? "redo" : "undo");
        item.Enabled = item.Reason.empty();
        return item;
    }

    [[nodiscard]] inline std::optional<std::string> PlayingToast(const bool redo, const bool hasScene, const bool playing)
    {
        if (!hasScene || !playing) return std::nullopt;
        return std::format("Pause the simulation to {}", redo ? "redo" : "undo");
    }

    [[nodiscard]] inline std::string FormatAge(const std::chrono::steady_clock::duration age)
    {
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(age).count();
        if (seconds < 2) return "just now";
        if (seconds < 60) return std::format("{} s ago", seconds);
        if (seconds < 3600) return std::format("{} min ago", seconds / 60);
        if (seconds < 86400) return std::format("{} h ago", seconds / 3600);
        return std::format("{} d ago", seconds / 86400);
    }

    struct HistoryRow
    {
        std::string Label;
        std::string Age;
        bool Current = false;
        bool Future = false;
        std::size_t Steps = 0;
    };

    [[nodiscard]] inline std::vector<HistoryRow> BuildHistoryRows(const GPP::HistoryView& view,
                                                                 const std::chrono::steady_clock::time_point now)
    {
        const auto label = [](const std::string& text) { return text.empty() ? std::string("(unnamed edit)") : text; };
        std::vector<HistoryRow> rows;
        for (std::size_t i = view.Redo.size(); i-- > 0;)
        {
            const auto& item = view.Redo[i];
            rows.push_back({label(item.Label), FormatAge(now - item.Time), false, true, i + 1});
        }
        for (std::size_t i = 0; i < view.Undo.size(); ++i)
        {
            const auto& item = view.Undo[i];
            rows.push_back({label(item.Label), FormatAge(now - item.Time), i == 0, false, i});
        }
        if (view.Undo.size() == view.UndoCount) rows.push_back({"Original state", {}, view.UndoCount == 0, false, view.UndoCount});
        return rows;
    }

    [[nodiscard]] inline std::string CountLabel(const std::string_view verb, const std::vector<std::string>& names)
    {
        if (names.empty()) return std::format("{} Entity", verb);
        if (names.size() == 1) return std::format("{} {}", verb, ShortenLabel(names.front(), 28));
        return std::format("{} {} Entities", verb, names.size());
    }

    [[nodiscard]] inline std::string SetFieldLabel(const std::string_view field, const std::string_view entity)
    {
        return entity.empty() ? std::format("Set {}", field)
                              : std::format("Set {} ({})", field, ShortenLabel(entity, 28));
    }
}
