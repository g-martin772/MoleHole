import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    constexpr float kGap = 18.0f;
    constexpr float kMinMiddle = 90.0f;
    constexpr float kChipPadding = 12.0f;
    constexpr std::size_t kShaderPopupLines = 20;
    constexpr const char* kShaderPopup = "##ShaderErrors";

    ImVec4 ToneColor(const StatusTone tone)
    {
        switch (tone)
        {
        case StatusTone::Dim: return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        case StatusTone::Good: return {0.45f, 0.82f, 0.5f, 1.0f};
        case StatusTone::Warning: return {0.96f, 0.72f, 0.3f, 1.0f};
        case StatusTone::Error: return {1.0f, 0.45f, 0.42f, 1.0f};
        case StatusTone::Accent: return {0.45f, 0.72f, 1.0f, 1.0f};
        default: return ImGui::GetStyleColorVec4(ImGuiCol_Text);
        }
    }

    ImVec4 KindColor(const NotificationKind kind)
    {
        switch (kind)
        {
        case NotificationKind::Success: return ToneColor(StatusTone::Good);
        case NotificationKind::Warning: return ToneColor(StatusTone::Warning);
        case NotificationKind::Error: return ToneColor(StatusTone::Error);
        default: return ToneColor(StatusTone::Normal);
        }
    }

    float MeasureText(const std::string& text)
    {
        return ImGui::CalcTextSize(text.c_str()).x;
    }

    struct StatusBarLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, SceneManager, UiState, Renderer, RenderSettingsService>;

        StatusBarLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<SceneManager> scenes,
                       std::shared_ptr<UiState> uiState, std::shared_ptr<Renderer> renderer,
                       std::shared_ptr<RenderSettingsService> renderSettings)
            : HotReloadableLayer(logger), m_Scenes(std::move(scenes)), m_UiState(std::move(uiState)),
              m_Renderer(std::move(renderer)), m_RenderSettings(std::move(renderSettings))
        {
        }

        void OnUiRender() override
        {
            const double now = ImGui::GetTime();
            m_Notifications.Observe(m_Logger->Notifications()->Toasts(), now);
            RefreshShaderFailures();
            if (!m_UiState->ShowStatusBar) return;

            const auto model = BuildModel();
            const auto items = BuildStatusItems(model);

            ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float height = ImGui::GetFrameHeight() + 2.0f;
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking |
                                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus |
                                               ImGuiWindowFlags_NoFocusOnAppearing;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.075f, 1.0f));
            const bool open = ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, height, flags);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
            if (open)
            {
                Draw(items, now, height);
                DrawShaderPopup();
            }
            ImGui::End();
        }

    private:
        void RefreshShaderFailures()
        {
            auto& diagnostics = ShaderDiagnostics::Instance();
            const auto revision = diagnostics.Revision();
            if (revision == m_ShaderRevision) return;
            m_ShaderRevision = revision;
            m_ShaderFailures = diagnostics.Failures();
        }

        StatusBarModel BuildModel() const
        {
            using Sim = StatusBarModel::SimState;
            StatusBarModel model;
            auto& ui = *m_UiState;

            const auto runner = ui.CurrentSceneName.empty() ? nullptr : m_Scenes->GetSimulation(ui.CurrentSceneName);
            if (runner)
            {
                const auto stats = runner->GetStats();
                model.Sim = ui.PlaySessionActive ? (stats.Paused ? Sim::Paused : Sim::Playing) : Sim::Stopped;
                model.Tick = stats.CurrentTick;
                model.TickRateHz = stats.TargetTickRate;
                model.TickStalled = stats.Stalled;
                model.TickOverrun = stats.Overrunning;
                if (const auto snapshot = runner->AcquireSnapshot()) model.SimTimeSeconds = snapshot->Clock().Time;
            }

            if (const auto stats = m_Renderer->GetBufferTargetStats(kViewportBufferId); stats && stats->Visible)
            {
                model.GpuMs = stats->GpuMs;
                model.Fps = stats->FramesPerSecond;
            }
            model.ResolutionLowered = ui.RenderScaleApplied < 0.999f;
            model.TracerPercent = m_RenderSettings->Effective().ResolutionScale * ui.RenderScaleApplied;

            model.Dirty = runner && ui.SceneState.IsDirty();
            model.AutosaveEnabled = ui.AutosaveEnabled;
            model.AutosaveFailing = ui.Autosave.Failing();
            if (const auto saved = ui.SceneState.SavedAt())
            {
                model.SecondsSinceSave = std::chrono::duration<double>(AutosaveScheduler::Clock::now() - *saved).count();
            }

            model.Exporting = ui.ExportActive;
            model.ExportProgress = ui.ExportProgress;

            if (const auto summary = SummarizeShaderFailures(m_ShaderFailures)) FillShaderLabels(model, *summary);
            return model;
        }

        void Draw(const StatusBarItems& items, const double now, const float height)
        {
            const ImVec2 origin = ImGui::GetWindowPos();
            const float width = ImGui::GetWindowWidth();
            constexpr float margin = 10.0f;
            const float inner = std::max(0.0f, width - margin * 2.0f);

            const auto layout = LayoutStatusBar(items, inner, kGap, kMinMiddle, kChipPadding, MeasureText);
            const float textY = (height - ImGui::GetTextLineHeight()) * 0.5f;

            float x = margin;
            for (std::size_t n = 0; n < layout.Left.size(); ++n)
            {
                const auto& item = items.Left[layout.Left[n]];
                x += DrawItem(item, layout.LeftVariant[n], origin, x, textY, height) + kGap;
            }

            std::vector<float> widths;
            float rightWidth = 0.0f;
            for (std::size_t n = 0; n < layout.Right.size(); ++n)
            {
                const auto& item = items.Right[layout.Right[n]];
                widths.push_back(MeasureText(item.Text[layout.RightVariant[n]]) + (item.Chip ? kChipPadding : 0.0f));
                rightWidth += widths.back() + (n > 0 ? kGap : 0.0f);
            }
            float rx = margin + inner - rightWidth;
            for (std::size_t n = 0; n < layout.Right.size(); ++n)
            {
                const auto& item = items.Right[layout.Right[n]];
                DrawItem(item, layout.RightVariant[n], origin, rx, textY, height);
                rx += widths[n] + kGap;
            }

            const float middleStart = x;
            const float middleWidth = std::max(0.0f, margin + inner - rightWidth - kGap - middleStart);
            DrawNotification(origin, middleStart, middleWidth, textY, height, now);
        }

        float DrawItem(const StatusItem& item, const std::size_t variant, const ImVec2 origin, const float x,
                       const float textY, const float height)
        {
            const auto& text = item.Text[variant];
            const float textWidth = MeasureText(text);
            const float width = textWidth + (item.Chip ? kChipPadding : 0.0f);
            auto* list = ImGui::GetWindowDrawList();
            const auto color = ToneColor(item.Tone);

            ImGui::SetCursorPos(ImVec2(x, 0.0f));
            ImGui::PushID(item.Id.c_str());
            ImGui::InvisibleButton("##item", ImVec2(std::max(width, 1.0f), height));
            const bool hovered = ImGui::IsItemHovered();
            const bool clicked = ImGui::IsItemClicked();
            ImGui::PopID();

            if (item.Chip)
            {
                const ImVec2 min(origin.x + x, origin.y + 3.0f);
                const ImVec2 max(origin.x + x + width, origin.y + height - 3.0f);
                const float fill = hovered && item.Clickable ? 0.38f : 0.24f;
                list->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, fill)), 4.0f);
                list->AddRect(min, max, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.7f)), 4.0f);
            }
            list->AddText(ImVec2(origin.x + x + (item.Chip ? kChipPadding * 0.5f : 0.0f), origin.y + textY), ImGui::GetColorU32(color),
                          text.c_str());

            if (hovered && !item.Tooltip.empty()) ImGui::SetTooltip("%s", item.Tooltip.c_str());
            if (clicked && item.Id == "shader-error")
            {
                m_PopupRequested = true;
                m_PopupAnchor = ImVec2(origin.x + x + width, origin.y);
            }
            return width;
        }

        void DrawNotification(const ImVec2 origin, const float x, const float width, const float textY,
                              const float height, const double now)
        {
            const auto shown = m_Notifications.Current(now);
            if (!shown || width < 24.0f) return;
            const auto text = TruncateToWidth(shown->Text, width, MeasureText);
            if (text.empty()) return;

            auto color = KindColor(shown->Kind);
            color.w = shown->Alpha;
            ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + x, origin.y + textY), ImGui::GetColorU32(color),
                                                text.c_str());
            ImGui::SetCursorPos(ImVec2(x, 0.0f));
            ImGui::InvisibleButton("##notification", ImVec2(MeasureText(text), height));
            if (ImGui::IsItemClicked()) m_UiState->ShowLogWindow = true;
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s\nClick to open the Log", shown->Text.c_str());
            }
        }

        void DrawShaderPopup()
        {
            if (std::exchange(m_PopupRequested, false)) ImGui::OpenPopup(kShaderPopup);
            ImGui::SetNextWindowPos(m_PopupAnchor, ImGuiCond_Appearing, ImVec2(1.0f, 1.0f));
            const float maxWidth = std::max(120.0f, ImGui::GetMainViewport()->Size.x - 32.0f);
            ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(std::min(760.0f, maxWidth), std::numeric_limits<float>::max()));
            if (!ImGui::BeginPopup(kShaderPopup)) return;

            if (m_ShaderFailures.empty())
            {
                ImGui::TextDisabled("No shader errors");
            }
            else
            {
                const auto& failure = m_ShaderFailures.front();
                ImGui::TextColored(ToneColor(StatusTone::Error), "Shader compile failed: %s", failure.Source.c_str());
                bool truncated = false;
                const auto text = FirstLines(failure.Message, kShaderPopupLines, &truncated);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(740.0f, ImGui::GetMainViewport()->Size.x - 64.0f));
                ImGui::TextUnformatted(text.c_str());
                if (truncated) ImGui::TextDisabled("... (see the Log for the full message)");
                ImGui::PopTextWrapPos();
                if (const auto summary = SummarizeShaderFailures(m_ShaderFailures); summary && summary->Extra > 0)
                {
                    ImGui::TextDisabled("+%zu more failing locations (see the Log)", summary->Extra);
                }
            }
            ImGui::Separator();
            if (ImGui::Button("Open Log"))
            {
                m_UiState->ShowLogWindow = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<RenderSettingsService> m_RenderSettings;
        NotificationTracker m_Notifications;
        std::vector<ShaderFailure> m_ShaderFailures;
        std::uint64_t m_ShaderRevision = std::numeric_limits<std::uint64_t>::max();
        ImVec2 m_PopupAnchor{0.0f, 0.0f};
        bool m_PopupRequested = false;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(StatusBarLayer)
