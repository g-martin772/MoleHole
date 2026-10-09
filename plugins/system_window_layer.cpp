#include <vulkan/vulkan.hpp>
#include <imgui.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    ImVec4 RateColor(const double value, const double good, const double ok)
    {
        return value >= good ? ImVec4(0.2f, 0.9f, 0.2f, 1.0f)
               : value >= ok ? ImVec4(0.9f, 0.9f, 0.2f, 1.0f)
                             : ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
    }

    struct SystemWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, Renderer, SceneManager, UiState>;

        SystemWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<Renderer> renderer,
                          std::shared_ptr<SceneManager> scenes, std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_Renderer(std::move(renderer)), m_Scenes(std::move(scenes)),
              m_UiState(std::move(uiState))
        {
        }

        void OnUiRender() override
        {
            if (!m_UiState->ShowSystemWindow) return;
            if (!ImGui::Begin("System", &m_UiState->ShowSystemWindow))
            {
                ImGui::End();
                return;
            }

            SectionHeader("PERFORMANCE");
            const auto ui = m_Renderer->GetUiStats();
            m_FrameTimeHistory[m_FrameTimeIndex] = static_cast<float>(ui.FrameMs);
            m_FrameTimeIndex = (m_FrameTimeIndex + 1) % IM_ARRAYSIZE(m_FrameTimeHistory);

            ImGui::TextColored(RateColor(ui.FramesPerSecond, 55.0, 30.0), "UI:         %6.1f fps   %5.2f ms",
                               ui.FramesPerSecond, ui.AverageFrameMs);
            ImGui::PlotHistogram("##FrameTime", m_FrameTimeHistory, IM_ARRAYSIZE(m_FrameTimeHistory),
                                 m_FrameTimeIndex, nullptr, 0.0f, 33.3f, ImVec2(-1, 60));

            if (const auto viewport = m_Renderer->GetBufferTargetStats(kViewportBufferId))
            {
                if (!viewport->Visible)
                {
                    ImGui::TextDisabled("Viewport:   paused (panel hidden)");
                }
                else
                {
                    ImGui::TextColored(RateColor(viewport->FramesPerSecond, 30.0, 10.0),
                                       "Viewport:   %6.1f fps   %7.1f ms", viewport->FramesPerSecond,
                                       viewport->AverageFrameMs);
                    ImGui::TextDisabled("            gpu %.1f ms in %u submissions  (%ux%u)", viewport->GpuMs,
                                        viewport->Submissions, viewport->Extent.x, viewport->Extent.y);
                    if (viewport->CurrentFrameMs > 1000.0)
                    {
                        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f),
                                           "            frame in progress for %.1f s", viewport->CurrentFrameMs / 1000.0);
                    }
                }
            }

            const auto runner = m_UiState->CurrentSceneName.empty()
                                    ? nullptr
                                    : m_Scenes->GetSimulation(m_UiState->CurrentSceneName);
            if (runner)
            {
                const auto sim = runner->GetStats();
                if (sim.Paused)
                {
                    ImGui::TextDisabled("Simulation: paused  (target %.0f Hz)", sim.TargetTickRate);
                }
                else
                {
                    ImGui::TextColored(RateColor(sim.TicksPerSecond, sim.TargetTickRate * 0.95,
                                                 sim.TargetTickRate * 0.5),
                                       "Simulation: %6.1f ups   %5.2f ms/tick  (target %.0f Hz)", sim.TicksPerSecond,
                                       sim.AverageTickMs, sim.TargetTickRate);
                    if (sim.Stalled)
                    {
                        ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f),
                                           "            STALLED: one tick has been running for %.1f s - showing the last published state",
                                           sim.CurrentTickMs / 1000.0);
                    }
                    else if (sim.Overrunning)
                    {
                        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f),
                                           "            ticks take longer than the tick period (max %.1f ms): the simulation runs slower than real time",
                                           sim.MaxTickMs);
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("Simulation: no active scene");
            }

            SectionHeader("SIMULATION");
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

            SectionHeader("SYSTEM INFO");
            const auto properties = m_Renderer->GetDevice()->GetPhysicalDevice().getProperties();
            ImGui::Text("GPU: %s", properties.deviceName.data());
            ImGui::Text("Driver Version: %u", properties.driverVersion);
            ImGui::Text("API Version: %u.%u.%u", VK_API_VERSION_MAJOR(properties.apiVersion),
                        VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion));

            if (const auto target = m_Renderer->GetRenderTargetInfo(kViewportBufferId))
            {
                ImGui::Text("Viewport Resolution: %ux%u", target->Extent.width, target->Extent.height);
            }

            ImGui::End();

            ImGui::ShowMetricsWindow();
        }

    private:
        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiState> m_UiState;
        float m_FrameTimeHistory[60] = {};
        int m_FrameTimeIndex = 0;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(SystemWindowLayer)
