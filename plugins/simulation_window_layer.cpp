#include <imgui.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    bool PrecisionCombo(const char* id, Units::Precision* value,
                        const std::function<const char*(Units::Precision)>& unavailableReason)
    {
        bool changed = false;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
        if (ImGui::BeginCombo(id, std::string(Units::PrecisionName(*value)).c_str()))
        {
            for (const auto precision : Units::kPrecisions)
            {
                const char* reason = unavailableReason(precision);
                std::string label(Units::PrecisionName(precision));
                if (reason) label += " (unavailable)";
                if (ImGui::Selectable(label.c_str(), precision == *value, reason ? ImGuiSelectableFlags_Disabled : 0))
                {
                    *value = precision;
                    changed = true;
                }
                if (reason && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", reason);
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    struct SimulationWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, SceneManager, UiState>;

        SimulationWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<SceneManager> scenes,
                              std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_Scenes(std::move(scenes)), m_UiState(std::move(uiState))
        {
        }

        void OnAttach() override { EnsureIconFont(*m_UiState); }

        void OnUiRender() override
        {
            if (!m_UiState->ShowSimulationWindow) return;
            if (!ImGui::Begin("Simulation", &m_UiState->ShowSimulationWindow))
            {
                ImGui::End();
                return;
            }

            const auto runner = m_UiState->CurrentSceneName.empty()
                                    ? nullptr
                                    : m_Scenes->GetSimulation(m_UiState->CurrentSceneName);
            DrawTiming(runner);
            DrawTickRate();
            DrawPhysics(runner);
            DrawPrecision();
            ImGui::End();
        }

    private:
        void DrawTiming(const std::shared_ptr<SimulationRunner>& runner)
        {
            SectionHeader("TIMING");
            if (!runner)
            {
                ImGui::TextDisabled("No active scene");
                return;
            }

            const auto sim = runner->GetStats();
            ImGui::Text("Status:    %s", sim.Paused ? "paused" : "running");
            ImGui::Text("Tick:      %llu", static_cast<unsigned long long>(sim.CurrentTick));
            ImGui::Text("Target:    %.0f Hz", sim.TargetTickRate);
            if (sim.Paused)
            {
                ImGui::TextDisabled("Actual:    paused");
                return;
            }

            ImGui::TextColored(RateColor(sim.TicksPerSecond, sim.TargetTickRate * 0.95, sim.TargetTickRate * 0.5),
                               "Actual:    %6.1f ups   %5.2f ms/tick  (max %.2f ms)", sim.TicksPerSecond,
                               sim.AverageTickMs, sim.MaxTickMs);
            if (sim.Stalled)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                ImGui::TextWrapped("STALLED: one tick has been running for %.1f s - showing the last published state",
                                   sim.CurrentTickMs / 1000.0);
                ImGui::PopStyleColor();
            }
            else if (sim.Overrunning)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.6f, 0.2f, 1.0f));
                ImGui::TextWrapped("ticks take longer than the tick period (max %.1f ms): the simulation runs slower than real time",
                                   sim.MaxTickMs);
                ImGui::PopStyleColor();
            }
        }

        void DrawTickRate()
        {
            SectionHeader("TICK RATE");
            float tickRate = m_UiState->SimulationTickRate;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##TickRate", &tickRate, static_cast<float>(SimulationRunner::kMinTickRate),
                                   1000.0f, "Tick rate: %.0f Hz", ImGuiSliderFlags_Logarithmic))
            {
                m_UiState->SimulationTickRate = std::clamp(tickRate, static_cast<float>(SimulationRunner::kMinTickRate),
                                                           static_cast<float>(SimulationRunner::kMaxTickRate));
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("How many times per second the simulation steps (Ctrl+click to type a value).\n"
                                  "Independent of the UI and viewport frame rates.");
            }
            for (const float preset : {30.0f, 60.0f, 120.0f, 240.0f})
            {
                char label[16];
                std::snprintf(label, sizeof(label), "%.0f Hz", preset);
                if (ImGui::SmallButton(label)) m_UiState->SimulationTickRate = preset;
                ImGui::SameLine();
            }
            ImGui::NewLine();
        }

        void DrawPhysics(const std::shared_ptr<SimulationRunner>& runner)
        {
            SectionHeader("PHYSICS");
            ImFont* icons = m_UiState->IconFont;

            float gravity = m_UiState->GravityMultiplier;
            if (PropertyRow(icons, "Gravity Strength", gravity != 1.0f, [&]
            {
                DragFloatValue("##v", &gravity, 0.01f, 0.0f, 10.0f, "%.2fx");
            }))
            {
                gravity = 1.0f;
            }
            m_UiState->GravityMultiplier = gravity;

            if (!runner)
            {
                ImGui::TextDisabled("Time Scale needs an active scene");
                return;
            }

            double timeScale = runner->Metadata().TimeScale;
            if (PropertyRow(icons, "Time Scale", timeScale != 1.0, [&]
            {
                if (QuantityValue("##timescale", &timeScale, Units::Dimension::None, 1.0e-6, 1.0e-12, 1.0e12))
                {
                    runner->EnqueueEdit([timeScale](Scene& scene) { scene.SetTimeScale(timeScale); });
                }
            }, "Physical (SI) seconds per second of simulation clock. 1 = real time."))
            {
                runner->EnqueueEdit([](Scene& scene) { scene.SetTimeScale(1.0); });
            }
        }

        void DrawPrecision()
        {
            SectionHeader("PRECISION");
            Units::Precision cpuPrecision = m_UiState->SimulationPrecision;
            if (PrecisionCombo("##cpuprecision", &cpuPrecision, [](Units::Precision p) -> const char*
                {
                    return Units::CpuSupports(p) ? nullptr : "This compiler has no std::float128_t";
                }))
            {
                m_UiState->SimulationPrecision = cpuPrecision;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Arithmetic type of MoleHole's gravity evaluation. Positions are stored in\n"
                                  "double either way; with f32 they are cast before subtraction.\n"
                                  "PhysX integration stays float32 (local origin).");
            }
            TextDisabledWrapped("GPU precision is a render setting: Render -> Transport");
        }

        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiState> m_UiState;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(SimulationWindowLayer)
