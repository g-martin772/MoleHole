import GPP;
import MoleHole;
import glm;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    constexpr const char* kIconPlay = "\xef\x81\x8b"; // U+F04B
    constexpr const char* kIconPause = "\xef\x81\x8c"; // U+F04C
    constexpr const char* kIconStop = "\xef\x81\x8d"; // U+F04D
    constexpr const char* kIconStep = "\xef\x81\x91"; // U+F051
    constexpr const char* kIconSnapshot = "\xef\x80\xb0"; // U+F030

    constexpr float kButtonHeight = 34.0f;
    constexpr float kIconWidth = 38.0f;
    constexpr float kGroupGap = 14.0f;
    constexpr double kMinTimeScale = 1.0e-12;
    constexpr double kMaxTimeScale = 1.0e12;

    constexpr ImVec4 kAccent{180.0f / 255.0f, 100.0f / 255.0f, 40.0f / 255.0f, 1.0f};
    constexpr ImVec4 kAccentHover{200.0f / 255.0f, 120.0f / 255.0f, 50.0f / 255.0f, 1.0f};
    constexpr ImVec4 kIdle{0.16f, 0.16f, 0.16f, 1.0f};
    constexpr ImVec4 kIdleHover{0.26f, 0.26f, 0.26f, 1.0f};

    struct Frame
    {
        std::shared_ptr<SimulationRunner> Runner;
        bool Session = false;
        bool Paused = true;
        double TimeScale = 1.0;
        double SimTime = 0.0;
        std::vector<BookmarkInfo> Bookmarks;
    };

    struct SimulationControlsLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, SceneManager, UiState>;

        SimulationControlsLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<SceneManager> scenes,
                                std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_Scenes(std::move(scenes)), m_UiState(std::move(uiState))
        {
        }

        void OnAttach() override
        {
            EnsureIconFont(*m_UiState);
        }

        void OnUiRender() override
        {
            if (!m_UiState->ViewportVisible) return;
            if (m_UiState->CurrentSceneName.empty()) return;
            const auto runner = m_Scenes->GetSimulation(m_UiState->CurrentSceneName);
            if (!runner) return;

            const glm::vec2 min = m_UiState->ViewportScreenMin;
            const glm::vec2 max = m_UiState->ViewportScreenMax;
            if (max.x - min.x < 72.0f || max.y - min.y < 1.0f) return;

            Frame frame;
            frame.Runner = runner;
            frame.Session = m_UiState->PlaySessionActive;
            frame.Paused = runner->IsPaused();
            frame.TimeScale = runner->Metadata().TimeScale;
            frame.Bookmarks = m_UiState->Bookmarks.Get();
            if (const auto snapshot = runner->AcquireSnapshot()) frame.SimTime = snapshot->Clock().Time;

            HandleShortcuts(frame);

            const float padding = 10.0f;
            const float right = std::min(max.x - 16.0f, m_UiState->ViewportToolbarLeft - 8.0f);
            const auto widths = TierWidths(frame);
            const auto tier = PickTransportTier(right - min.x - 16.0f - 2.0f * padding, widths);
            const float barWidth = widths[tier] + 2.0f * padding;
            const float left = std::clamp((min.x + max.x - barWidth) * 0.5f, min.x + 16.0f, std::max(min.x + 16.0f, right - barWidth));
            ImGui::SetNextWindowPos(ImVec2(left, min.y + 16.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 0.85f));

            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking |
                                               ImGuiWindowFlags_AlwaysAutoResize |
                                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings;

            if (ImGui::Begin("##SimulationControls", nullptr, flags))
            {
                switch (tier)
                {
                case 0: DrawBar(frame, true, true, true); break;
                case 1: DrawBar(frame, false, true, true); break;
                case 2: DrawBar(frame, false, true, false); break;
                default: DrawBar(frame, false, false, false); break;
                }
                DrawPopups(frame);
            }
            ImGui::End();

            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }

    private:
        static float TextButtonWidth(const std::string& label)
        {
            return ImGui::CalcTextSize(label.c_str()).x + 16.0f;
        }

        std::string StepLabel() const { return std::format("+{}", m_UiState->TransportStepCount); }

        static std::string CustomSpeedLabel(const Frame& frame)
        {
            return MatchSpeedPreset(frame.TimeScale) ? "x..." : FormatSpeed(frame.TimeScale);
        }

        static float ReadoutWidth()
        {
            return ImGui::CalcTextSize("t = 00 d 00:00:00").x + 16.0f;
        }

        std::array<float, 4> TierWidths(const Frame& frame) const
        {
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float transport = 3.0f * kIconWidth + 2.0f * gap;
            const float stepN = gap + TextButtonWidth(StepLabel());
            float presets = 0.0f;
            for (const double preset : kSpeedPresets) presets += gap + TextButtonWidth(FormatSpeed(preset));
            presets += gap + TextButtonWidth(CustomSpeedLabel(frame));
            const float combo = kGroupGap + TextButtonWidth(FormatSpeed(frame.TimeScale));
            const float readout = kGroupGap + ReadoutWidth();
            const float snapshot = kGroupGap + kIconWidth + gap + TextButtonWidth("00");
            return {
                transport + stepN + kGroupGap - gap + presets + readout + snapshot,
                transport + stepN + combo + readout + snapshot,
                transport + combo,
                kIconWidth,
            };
        }

        void DrawBar(const Frame& frame, const bool presets, const bool combo, const bool readout)
        {
            const bool canStep = frame.Paused;
            DrawPlay(frame);
            if (!combo) return;

            ImGui::SameLine();
            ImGui::BeginDisabled(!frame.Session);
            if (IconButton(kIconStop, "[]", kIdle, kIdleHover))
            {
                m_UiState->PendingStopSimulation = true;
            }
            ImGui::EndDisabled();
            Tip("Stop: restore the scene to how it was when Play was pressed");

            ImGui::SameLine();
            ImGui::BeginDisabled(!canStep);
            if (IconButton(kIconStep, ">|", kIdle, kIdleHover)) RequestStep(false);
            ImGui::EndDisabled();
            Tip("Step one tick (.), only while paused.\nFrom Edit mode this starts a paused Play session: Stop restores the scene.");

            if (readout)
            {
                ImGui::SameLine();
                ImGui::BeginDisabled(!canStep);
                if (ImGui::Button(StepLabel().c_str(), ImVec2(TextButtonWidth(StepLabel()), kButtonHeight)))
                {
                    RequestStep(true);
                }
                ImGui::EndDisabled();
                Tip("Step N ticks (Shift+.). Right-click to change N.");
                if (ImGui::BeginPopupContextItem("##stepcount"))
                {
                    ImGui::SetNextItemWidth(120.0f);
                    int count = m_UiState->TransportStepCount;
                    if (ImGui::InputInt("Ticks per step", &count, 1, 10))
                    {
                        m_UiState->TransportStepCount = static_cast<std::int32_t>(ClampStepCount(count));
                    }
                    ImGui::EndPopup();
                }
            }

            if (presets)
            {
                ImGui::SameLine(0.0f, kGroupGap);
                const auto matched = MatchSpeedPreset(frame.TimeScale);
                for (std::size_t i = 0; i < kSpeedPresets.size(); ++i)
                {
                    if (i > 0) ImGui::SameLine();
                    const auto label = FormatSpeed(kSpeedPresets[i]);
                    if (SpeedButton(label, matched == i)) SetTimeScale(frame, kSpeedPresets[i]);
                    Tip(std::format("Time scale {} (physical seconds per simulation second)", label));
                }
                ImGui::SameLine();
                if (SpeedButton(CustomSpeedLabel(frame), !matched)) m_OpenSpeed = true;
                Tip("Custom time scale");
            }
            else if (combo)
            {
                ImGui::SameLine(0.0f, kGroupGap);
                if (SpeedButton(FormatSpeed(frame.TimeScale), true)) m_OpenSpeed = true;
                Tip("Time scale ([ or numpad - slower, ] or numpad + faster)");
            }

            if (readout)
            {
                ImGui::SameLine(0.0f, kGroupGap);
                const auto label = std::format("t = {}##readout", FormatSimTime(frame.SimTime));
                if (ImGui::Button(label.c_str(), ImVec2(ReadoutWidth(), kButtonHeight))) m_OpenJump = true;
                Tip("Simulation time (physical seconds of a distant observer). Click to jump to a time.");

                ImGui::SameLine(0.0f, kGroupGap);
                if (IconButton(kIconSnapshot, "S", kIdle, kIdleHover))
                {
                    m_UiState->Transport.Push({TransportRequest::Kind::Snapshot});
                }
                Tip("Snapshot: keep the current simulation state in memory (up to 8, not saved with the scene)");
                ImGui::SameLine();
                const auto count = std::format("{}##snapshots", frame.Bookmarks.size());
                ImGui::BeginDisabled(frame.Bookmarks.empty());
                if (ImGui::Button(count.c_str(), ImVec2(TextButtonWidth("00"), kButtonHeight))) m_OpenSnapshots = true;
                ImGui::EndDisabled();
                Tip("Restore a snapshot");
            }
        }

        void DrawPlay(const Frame& frame)
        {
            const bool paused = frame.Paused;
            if (IconButton(paused ? kIconPlay : kIconPause, paused ? ">" : "||", kAccent, kAccentHover))
            {
                if (paused) m_UiState->PendingSnapshotForPlay = true;
                else frame.Runner->SetPaused(true);
            }
            Tip(paused ? "Resume" : "Pause");
        }

        void DrawPopups(const Frame& frame)
        {
            if (std::exchange(m_OpenSpeed, false)) ImGui::OpenPopup("##speed");
            if (std::exchange(m_OpenJump, false))
            {
                m_JumpText = {};
                ImGui::OpenPopup("##jump");
            }
            if (std::exchange(m_OpenSnapshots, false)) ImGui::OpenPopup("##snapshots");

            if (ImGui::BeginPopup("##speed"))
            {
                const auto matched = MatchSpeedPreset(frame.TimeScale);
                for (std::size_t i = 0; i < kSpeedPresets.size(); ++i)
                {
                    if (ImGui::Selectable(FormatSpeed(kSpeedPresets[i]).c_str(), matched == i))
                    {
                        SetTimeScale(frame, kSpeedPresets[i]);
                    }
                }
                ImGui::Separator();
                ImGui::TextDisabled("Time scale");
                double scale = frame.TimeScale;
                ImGui::SetNextItemWidth(160.0f);
                if (QuantityValue("##customscale", &scale, Units::Dimension::None, 1.0e-6, kMinTimeScale, kMaxTimeScale))
                {
                    SetTimeScale(frame, scale);
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("##jump"))
            {
                ImGui::TextDisabled("Jump to simulation time");
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(200.0f);
                const bool enter = ImGui::InputTextWithHint("##jumptime", "1.5 d, 3600 s, 2 yr", m_JumpText.data(),
                                                            m_JumpText.size(), ImGuiInputTextFlags_EnterReturnsTrue);
                const auto target = ParseJumpTime(m_JumpText.data());
                if (m_JumpText[0] == '\0') ImGui::TextDisabled("A plain number is seconds");
                else if (target) ImGui::TextDisabled("= %s", FormatSimTime(*target).c_str());
                else ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "Unrecognised time");
                ImGui::BeginDisabled(!target);
                if ((ImGui::Button("Jump") || enter) && target)
                {
                    frame.Runner->EnqueueSystemEdit([seconds = *target](Scene& scene) { scene.SetSimTime(seconds); }, false);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Zero"))
                {
                    frame.Runner->EnqueueSystemEdit([](Scene& scene) { scene.SetSimTime(0.0); }, false);
                    ImGui::CloseCurrentPopup();
                }
                TextDisabledWrapped("Moves the clock only: bodies keep their state.");
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("##snapshots"))
            {
                ImGui::TextDisabled("In-memory snapshots (lost on Stop or restart)");
                ImGui::Separator();
                for (std::size_t i = frame.Bookmarks.size(); i-- > 0;)
                {
                    const auto& bookmark = frame.Bookmarks[i];
                    const auto label = std::format("{}  t = {}   tick {}##{}", i + 1, bookmark.Name, bookmark.Tick, i);
                    if (ImGui::Selectable(label.c_str()))
                    {
                        m_UiState->Transport.Push({TransportRequest::Kind::Restore, 1, i});
                    }
                }
                if (frame.Bookmarks.empty()) ImGui::TextDisabled("No snapshots");
                ImGui::EndPopup();
            }
        }

        void HandleShortcuts(const Frame& frame)
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.WantTextInput || ImGui::IsAnyItemActive() || m_UiState->GraphEditorFocused) return;
            if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;

            if (frame.Paused)
            {
                if (ImGui::IsKeyChordPressed(ImGuiKey_Period, ImGuiInputFlags_Repeat)) RequestStep(false);
                else if (ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_Period, ImGuiInputFlags_Repeat)) RequestStep(true);
            }
            if (ImGui::IsKeyChordPressed(ImGuiKey_LeftBracket) || ImGui::IsKeyChordPressed(ImGuiKey_KeypadSubtract))
                SetTimeScale(frame, NextSpeedPreset(frame.TimeScale, -1));
            else if (ImGui::IsKeyChordPressed(ImGuiKey_RightBracket) || ImGui::IsKeyChordPressed(ImGuiKey_KeypadAdd))
                SetTimeScale(frame, NextSpeedPreset(frame.TimeScale, 1));
        }

        void RequestStep(const bool multi)
        {
            m_UiState->Transport.Push({TransportRequest::Kind::Step, StepTicks(multi, m_UiState->TransportStepCount)});
        }

        void SetTimeScale(const Frame& frame, const double scale)
        {
            const double clamped = std::clamp(scale, kMinTimeScale, kMaxTimeScale);
            if (frame.Session)
            {
                frame.Runner->EnqueueSystemEdit([clamped](Scene& scene) { scene.SetTimeScale(clamped); }, false);
            }
            else
            {
                frame.Runner->EnqueueEdit([clamped](Scene& scene) { scene.SetTimeScale(clamped); });
            }
        }

        bool IconButton(const char* icon, const char* fallback, const ImVec4& color, const ImVec4& hover)
        {
            const bool hasIconFont = m_UiState->IconFont != nullptr;
            ImGui::PushStyleColor(ImGuiCol_Button, color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (hasIconFont) ImGui::PushFont(m_UiState->IconFont);
            const bool pressed = ImGui::Button(hasIconFont ? icon : fallback, ImVec2(kIconWidth, kButtonHeight));
            if (hasIconFont) ImGui::PopFont();
            ImGui::PopStyleColor(3);
            return pressed;
        }

        static bool SpeedButton(const std::string& label, const bool active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, active ? kAccent : kIdle);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? kAccentHover : kIdleHover);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            const bool pressed = ImGui::Button((label + "##speed" + label).c_str(), ImVec2(TextButtonWidth(label), kButtonHeight));
            ImGui::PopStyleColor(3);
            return pressed;
        }

        static void Tip(const std::string& text)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text.c_str());
        }

        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiState> m_UiState;
        std::array<char, 64> m_JumpText{};
        bool m_OpenSpeed = false;
        bool m_OpenJump = false;
        bool m_OpenSnapshots = false;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(SimulationControlsLayer)
