#include <imgui.h>

import GPP;
import MoleHole;
import vulkan;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct SystemWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, Renderer, UiState, RenderSettingsService>;

        SystemWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<Renderer> renderer,
                          std::shared_ptr<UiState> uiState, std::shared_ptr<RenderSettingsService> renderSettings)
            : HotReloadableLayer(logger), m_Renderer(std::move(renderer)), m_UiState(std::move(uiState)),
              m_RenderSettings(std::move(renderSettings))
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

            DrawRenderStats();

            SectionHeader("SYSTEM INFO");
            const auto properties = m_Renderer->GetDevice()->GetPhysicalDevice().getProperties();
            ImGui::Text("GPU: %s", properties.deviceName.data());
            ImGui::Text("Driver Version: %u", properties.driverVersion);
            ImGui::Text("API Version: %u.%u.%u", vk::apiVersionMajor(properties.apiVersion),
                        vk::apiVersionMinor(properties.apiVersion), vk::apiVersionPatch(properties.apiVersion));

            if (const auto target = m_Renderer->GetRenderTargetInfo(kViewportBufferId))
            {
                ImGui::Text("Viewport Resolution: %ux%u", target->Extent.width, target->Extent.height);
            }

            DrawDevice();

            ImGui::End();

            ImGui::ShowMetricsWindow();
        }

    private:
        void DrawRenderStats()
        {
            SectionHeader("RENDER");
            const auto stats = m_Renderer->GetBufferTargetStats(kViewportBufferId);
            if (!stats)
            {
                ImGui::TextDisabled("Viewport not rendering.");
                return;
            }
            const auto& effective = m_RenderSettings->Effective();
            const auto context = m_RenderSettings->ActiveContext();
            ImGui::Text("%s -> %s", std::string(RenderContextLabel(context)).c_str(),
                        m_RenderSettings->ProfileFor(context).Name.c_str());
            const auto trace = ScaledTraceExtent(vk::Extent3D{stats->Extent.x, stats->Extent.y, 1},
                                                 effective.ResolutionScale * m_UiState->RenderGovernorScale);
            ImGui::Text("Tracer %ux%u (%.0f%% of the viewport)", trace.width, trace.height,
                        effective.ResolutionScale * m_UiState->RenderGovernorScale);
            if (m_UiState->RenderGovernorScale < 1.0f)
                ImGui::TextDisabled("Governor: %.0f%% of the profile's scale", m_UiState->RenderGovernorScale * 100.0f);
            ImGui::Text("Ray steps: up to %d at %.4f", effective.MaxRaySteps, effective.RayStepSize);
        }

        void DrawDevice()
        {
            const auto& caps = m_RenderSettings->Capabilities();
            SectionHeader("DEVICE");
            ImGui::TextWrapped("%s (%s)", caps.DeviceName.c_str(), std::string(GpuTypeName(caps.Type)).c_str());
            TextDisabledWrapped(caps.Reason);
            for (const auto key : GpuCapability::kAll)
            {
                const bool has = caps.Has(key);
                ImGui::TextColored(has ? ImVec4(0.45f, 0.8f, 0.45f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s  %s",
                                   has ? "yes" : "no ", std::string(CapabilityLabel(key)).c_str());
            }
        }

        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<RenderSettingsService> m_RenderSettings;
        float m_FrameTimeHistory[60] = {};
        int m_FrameTimeIndex = 0;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(SystemWindowLayer)
