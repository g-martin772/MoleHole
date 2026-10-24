export module MoleHole:UI.StatusBar;

import std;
import GPP;

export namespace MoleHole
{
    enum class StatusTone { Normal, Dim, Good, Warning, Error, Accent };

    struct StatusItem
    {
        std::string Id;
        std::vector<std::string> Text;
        int Priority = 0;
        StatusTone Tone = StatusTone::Normal;
        std::string Tooltip;
        bool Clickable = false;
        bool Chip = false;
    };

    struct StatusBarModel
    {
        enum class SimState { NoScene, Stopped, Playing, Paused } Sim = SimState::NoScene;
        std::uint64_t Tick = 0;
        double SimTimeSeconds = 0.0;
        double TickRateHz = 0.0;
        bool TickOverrun = false;
        bool TickStalled = false;

        std::optional<double> GpuMs;
        std::optional<double> Fps;
        float TracerPercent = 100.0f;
        bool ResolutionLowered = false;

        bool Dirty = false;
        bool AutosaveEnabled = false;
        bool AutosaveFailing = false;
        std::optional<double> SecondsSinceSave;

        bool Exporting = false;
        float ExportProgress = 0.0f;

        std::string ShaderLabel;
        std::string ShaderShortLabel;
    };

    struct StatusBarItems
    {
        std::vector<StatusItem> Left;
        std::vector<StatusItem> Right;
    };

    struct StatusLayout
    {
        std::vector<std::size_t> Left;
        std::vector<std::size_t> Right;
        std::vector<std::size_t> LeftVariant;
        std::vector<std::size_t> RightVariant;
        float MiddleWidth = 0.0f;
    };

    struct ShaderErrorSummary
    {
        std::string File;
        int Line = 0;
        std::size_t Extra = 0;
    };

    std::string FormatSimTime(const double seconds)
    {
        const double magnitude = std::abs(seconds);
        if (magnitude == 0.0) return "0 s";
        if (magnitude < 1e-3) return std::format("{:.3f} us", seconds * 1e6);
        if (magnitude < 1.0) return std::format("{:.3f} ms", seconds * 1e3);
        if (magnitude < 60.0) return std::format("{:.3f} s", seconds);
        constexpr double kYear = 365.25 * 86400.0;
        if (magnitude >= kYear) return std::format("{:.4f} yr", seconds / kYear);
        const auto whole = static_cast<long long>(seconds);
        const auto days = whole / 86400;
        const auto clock = std::format("{:02}:{:02}:{:02}", whole / 3600 % 24, whole / 60 % 60, whole % 60);
        return days > 0 ? std::format("{} d {}", days, clock) : clock;
    }

    std::string FormatAgo(const double seconds)
    {
        if (seconds < 5.0) return "just now";
        const auto whole = static_cast<long long>(seconds);
        if (whole < 60) return std::format("{} s ago", whole);
        if (whole < 3600) return std::format("{} min ago", whole / 60);
        if (whole < 86400) return std::format("{} h ago", whole / 3600);
        return std::format("{} d ago", whole / 86400);
    }

    std::string FormatCount(const std::uint64_t value)
    {
        auto digits = std::to_string(value);
        for (auto at = static_cast<std::ptrdiff_t>(digits.size()) - 3; at > 0; at -= 3) digits.insert(static_cast<std::size_t>(at), ",");
        return digits;
    }

    std::string FormatRate(const double hertz)
    {
        return hertz >= 100.0 || hertz == std::floor(hertz) ? std::format("{:.0f} Hz", hertz) : std::format("{:.1f} Hz", hertz);
    }

    std::string FirstLines(const std::string_view text, const std::size_t count, bool* truncated = nullptr)
    {
        std::size_t end = 0;
        std::size_t lines = 0;
        while (end < text.size() && lines < count)
        {
            const auto next = text.find('\n', end);
            ++lines;
            if (next == std::string_view::npos)
            {
                end = text.size();
                break;
            }
            end = next + 1;
        }
        if (truncated) *truncated = end < text.size();
        auto result = std::string(text.substr(0, end));
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
        return result;
    }

    template <typename Measure>
    std::string TruncateToWidth(const std::string& text, const float maxWidth, const Measure& measure)
    {
        if (maxWidth <= 0.0f) return {};
        if (measure(text) <= maxWidth) return text;
        constexpr std::string_view ellipsis = "...";
        const float ellipsisWidth = measure(std::string(ellipsis));
        if (ellipsisWidth > maxWidth) return {};
        std::size_t low = 0;
        std::size_t high = text.size();
        while (low < high)
        {
            const auto mid = (low + high + 1) / 2;
            std::size_t cut = mid;
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
            if (measure(text.substr(0, cut)) + ellipsisWidth <= maxWidth) low = mid;
            else high = mid - 1;
        }
        std::size_t cut = low;
        while (cut > 0 && cut < text.size() && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
        return text.substr(0, cut) + std::string(ellipsis);
    }

    std::optional<ShaderErrorSummary> SummarizeShaderFailures(const std::span<const GPP::ShaderFailure> failures)
    {
        if (failures.empty()) return std::nullopt;
        const auto name = [](const GPP::ShaderFailure& f) { return std::filesystem::path(f.File).filename().string(); };
        ShaderErrorSummary summary{name(failures.front()), failures.front().Line, 0};
        std::vector<std::pair<std::string, int>> seen{{summary.File, summary.Line}};
        for (const auto& failure : failures.subspan(1))
        {
            std::pair<std::string, int> key{name(failure), failure.Line};
            if (std::ranges::find(seen, key) == seen.end()) seen.push_back(std::move(key));
        }
        summary.Extra = seen.size() - 1;
        return summary;
    }

    void FillShaderLabels(StatusBarModel& model, const ShaderErrorSummary& summary)
    {
        const auto place = summary.Line > 0 ? std::format("{}:{}", summary.File, summary.Line) : summary.File;
        const auto extra = summary.Extra > 0 ? std::format(" (+{})", summary.Extra) : std::string{};
        model.ShaderLabel = std::format("Shader error: {}{}", place, extra);
        model.ShaderShortLabel = summary.Extra > 0 ? std::format("Shader error (+{})", summary.Extra) : "Shader error";
    }

    StatusBarItems BuildStatusItems(const StatusBarModel& m)
    {
        using Sim = StatusBarModel::SimState;
        StatusBarItems items;

        const bool hasScene = m.Sim != Sim::NoScene;
        const auto* state = "No scene";
        auto stateTone = StatusTone::Dim;
        switch (m.Sim)
        {
        case Sim::Playing: state = "Playing"; stateTone = StatusTone::Good; break;
        case Sim::Paused: state = "Paused"; stateTone = StatusTone::Warning; break;
        case Sim::Stopped: state = "Stopped"; break;
        case Sim::NoScene: break;
        }
        items.Left.push_back({"sim-state", {state}, 90, stateTone, "Simulation state", false});
        if (hasScene)
        {
            items.Left.push_back({"sim-time", {std::format("t = {}", FormatSimTime(m.SimTimeSeconds))}, 60, StatusTone::Normal,
                                  "Simulation time (coordinate time of a distant observer)", false});
            items.Left.push_back({"tick", {std::format("tick {}", FormatCount(m.Tick)), FormatCount(m.Tick)}, 50,
                                  StatusTone::Normal, "Simulation tick", false});
            auto rateTone = StatusTone::Dim;
            auto rateTip = std::string("Target simulation tick rate");
            if (m.TickStalled)
            {
                rateTone = StatusTone::Error;
                rateTip = "A tick has been running for over a second: the simulation is stalled";
            }
            else if (m.TickOverrun)
            {
                rateTone = StatusTone::Warning;
                rateTip = "Ticks take longer than the tick period: the simulation runs slower than real time";
            }
            items.Left.push_back({"tick-rate", {FormatRate(m.TickRateHz)}, 40, rateTone, rateTip, false});
        }

        if (!m.ShaderLabel.empty())
        {
            items.Right.push_back({"shader-error",
                                   {m.ShaderLabel, m.ShaderShortLabel, "!"},
                                   100,
                                   StatusTone::Error,
                                   "Shader failed to compile. Click for details",
                                   true,
                                   true});
        }
        if (m.Exporting)
        {
            items.Right.push_back({"export",
                                   {std::format("Export {:.0f}%", m.ExportProgress * 100.0f), std::format("{:.0f}%", m.ExportProgress * 100.0f)},
                                   85, StatusTone::Accent, "Export in progress", false, true});
        }
        if (m.ResolutionLowered)
        {
            items.Right.push_back({"governor",
                                   {std::format("scaled to {:.0f}%", m.TracerPercent), std::format("{:.0f}%", m.TracerPercent)},
                                   70, StatusTone::Warning,
                                   "The frame governor lowered the tracer resolution to stay within the profile's frame budget",
                                   false,
                                   true});
        }
        if (m.GpuMs)
        {
            items.Right.push_back({"gpu", {std::format("GPU {:.1f} ms", *m.GpuMs), std::format("{:.1f} ms", *m.GpuMs)}, 65,
                                   StatusTone::Normal, "Viewport GPU time of the last frame", false});
        }
        if (m.Fps)
        {
            items.Right.push_back({"fps", {std::format("{:.0f} fps", *m.Fps)}, 60, StatusTone::Normal, "Viewport frame rate", false});
        }
        if (hasScene)
        {
            std::string full;
            std::string brief;
            auto tone = StatusTone::Dim;
            std::string tip;
            if (m.AutosaveFailing)
            {
                full = "Autosave failed";
                brief = "Autosave!";
                tone = StatusTone::Error;
                tip = "The last autosave attempt failed; see the Log for the reason";
            }
            else
            {
                const auto saved = m.SecondsSinceSave ? ", saved " + FormatAgo(*m.SecondsSinceSave) : std::string{};
                full = std::format("Autosave {}{}", m.AutosaveEnabled ? "on" : "off", saved);
                brief = m.AutosaveEnabled ? "Autosave on" : "Autosave off";
                tip = m.AutosaveEnabled ? "Unsaved changes are written to the scene file shortly after an edit"
                                        : "Autosave is off (File > Auto Save). Recovery copies are still written";
            }
            std::vector<std::string> variants{full};
            if (brief != full) variants.push_back(brief);
            items.Right.push_back({"autosave", std::move(variants), 30, tone, tip, false});
        }
        if (m.Dirty)
        {
            items.Right.push_back({"dirty", {"*"}, 95, StatusTone::Warning, "Unsaved changes", false});
        }
        return items;
    }

    template <typename Measure>
    StatusLayout LayoutStatusBar(const StatusBarItems& items, const float width, const float gap, const float minMiddle,
                                 const float chipPadding, const Measure& measure)
    {
        struct Slot
        {
            const StatusItem* Item = nullptr;
            bool Right = false;
            std::size_t Index = 0;
            std::size_t Variant = 0;
            bool Visible = true;
        };
        std::vector<Slot> slots;
        for (std::size_t i = 0; i < items.Left.size(); ++i) slots.push_back({&items.Left[i], false, i, 0, true});
        for (std::size_t i = 0; i < items.Right.size(); ++i) slots.push_back({&items.Right[i], true, i, 0, true});

        const auto total = [&]
        {
            float sum = 0.0f;
            int count = 0;
            for (const auto& slot : slots)
            {
                if (!slot.Visible) continue;
                sum += measure(slot.Item->Text[slot.Variant]) + (slot.Item->Chip ? chipPadding : 0.0f);
                ++count;
            }
            return sum + (count > 1 ? static_cast<float>(count - 1) * gap : 0.0f);
        };

        while (total() + minMiddle > width)
        {
            Slot* lowest = nullptr;
            for (auto& slot : slots)
            {
                if (!slot.Visible) continue;
                if (!lowest || slot.Item->Priority < lowest->Item->Priority) lowest = &slot;
            }
            if (!lowest) break;
            if (lowest->Variant + 1 < lowest->Item->Text.size()) ++lowest->Variant;
            else lowest->Visible = false;
        }

        StatusLayout layout;
        for (const auto& slot : slots)
        {
            if (!slot.Visible) continue;
            auto& indices = slot.Right ? layout.Right : layout.Left;
            auto& variants = slot.Right ? layout.RightVariant : layout.LeftVariant;
            indices.push_back(slot.Index);
            variants.push_back(slot.Variant);
        }
        layout.MiddleWidth = std::max(0.0f, width - total() - gap * 2.0f);
        return layout;
    }

    class NotificationTracker
    {
    public:
        static constexpr double kHoldSeconds = 6.0;
        static constexpr double kFadeSeconds = 1.5;

        struct Shown
        {
            std::string Text;
            GPP::NotificationKind Kind = GPP::NotificationKind::Info;
            float Alpha = 1.0f;
        };

        void Observe(const std::span<const GPP::Notification> toasts, const double now)
        {
            const GPP::Notification* newest = nullptr;
            std::unordered_map<std::uint64_t, float> seen;
            for (const auto& toast : toasts)
            {
                seen.emplace(toast.Id, toast.AgeSeconds);
                const auto previous = m_Seen.find(toast.Id);
                const bool fresh = previous == m_Seen.end() || toast.AgeSeconds + 1e-4f < previous->second;
                if (!fresh) continue;
                if (!newest || toast.AgeSeconds < newest->AgeSeconds ||
                    (toast.AgeSeconds == newest->AgeSeconds && toast.Id > newest->Id))
                    newest = &toast;
            }
            m_Seen = std::move(seen);
            if (!newest) return;
            m_Text = Describe(*newest);
            m_Kind = newest->Kind;
            m_PostedAt = now;
        }

        [[nodiscard]] std::optional<Shown> Current(const double now) const
        {
            if (m_Text.empty()) return std::nullopt;
            const auto alpha = Alpha(now - m_PostedAt);
            if (alpha <= 0.0f) return std::nullopt;
            return Shown{m_Text, m_Kind, alpha};
        }

        [[nodiscard]] static float Alpha(const double age)
        {
            if (age < 0.0 || age >= kHoldSeconds + kFadeSeconds) return 0.0f;
            if (age <= kHoldSeconds) return 1.0f;
            return static_cast<float>(1.0 - (age - kHoldSeconds) / kFadeSeconds);
        }

        [[nodiscard]] static std::string Describe(const GPP::Notification& toast)
        {
            auto line = toast.Message.substr(0, toast.Message.find('\n'));
            return line.empty() ? toast.Title : toast.Title + ": " + line;
        }

    private:
        std::unordered_map<std::uint64_t, float> m_Seen;
        std::string m_Text;
        GPP::NotificationKind m_Kind = GPP::NotificationKind::Info;
        double m_PostedAt = 0.0;
    };
}
