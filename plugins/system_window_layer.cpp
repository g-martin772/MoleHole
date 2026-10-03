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
    struct SystemWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, Renderer, UiState>;

        SystemWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<Renderer> renderer,
                          std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_Renderer(std::move(renderer)), m_UiState(std::move(uiState))
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
            const float fps = ImGui::GetIO().Framerate;
            const float frameTime = fps > 0.0f ? 1000.0f / fps : 0.0f;
            m_FrameTimeHistory[m_FrameTimeIndex] = frameTime;
            m_FrameTimeIndex = (m_FrameTimeIndex + 1) % IM_ARRAYSIZE(m_FrameTimeHistory);

            ImVec4 fpsColor = fps >= 55.0f ? ImVec4(0.2f, 0.9f, 0.2f, 1.0f)
                             : fps >= 30.0f ? ImVec4(0.9f, 0.9f, 0.2f, 1.0f)
                                            : ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
            ImGui::TextColored(fpsColor, "FPS: %.0f", fps);
            ImGui::Text("Frame Time: %.2fms", frameTime);
            ImGui::PlotHistogram("##FrameTime", m_FrameTimeHistory, IM_ARRAYSIZE(m_FrameTimeHistory),
                                 m_FrameTimeIndex, nullptr, 0.0f, 33.3f, ImVec2(-1, 60));

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
        }

    private:
        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<UiState> m_UiState;
        float m_FrameTimeHistory[60] = {};
        int m_FrameTimeIndex = 0;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(SystemWindowLayer)
