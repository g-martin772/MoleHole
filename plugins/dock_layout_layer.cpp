#include <imgui.h>
#include <imgui_internal.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct DockLayoutLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, Renderer>;

        DockLayoutLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<Renderer> renderer)
            : HotReloadableLayer(logger), m_Renderer(std::move(renderer))
        {
        }

        void OnUiRender() override
        {
            if (m_Done) return;
            m_Done = true;

            if (std::filesystem::exists(".gpp/imgui_main.ini"))
            {
                m_Logger->Info("DockLayoutLayer: existing layout found, leaving it alone");
                return;
            }

            const auto dockspaceId = m_Renderer->GetMainDockspaceId();
            if (dockspaceId == 0) return;

            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_PassthruCentralNode);
            ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

            ImGuiID dockRight, dockCenter;
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Right, 0.28f, &dockRight, &dockCenter);

            ImGuiID dockRightTop, dockRightBottom;
            ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Up, 0.5f, &dockRightTop, &dockRightBottom);

            ImGuiID dockBottom;
            ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.30f, &dockBottom, &dockCenter);

            ImGui::DockBuilderDockWindow("System", dockRightTop);
            ImGui::DockBuilderDockWindow("Camera", dockRightTop);
            ImGui::DockBuilderDockWindow("Scene", dockRightBottom);
            ImGui::DockBuilderDockWindow("Debug", dockRightBottom);
            ImGui::DockBuilderDockWindow("Viewport", dockCenter);
            ImGui::DockBuilderDockWindow("General Relativity", dockBottom);
            ImGui::DockBuilderDockWindow("Science", dockBottom);

            ImGui::DockBuilderFinish(dockspaceId);
            m_Logger->Info("DockLayoutLayer: applied first-run docking layout");
        }

    private:
        std::shared_ptr<Renderer> m_Renderer;
        bool m_Done = false;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(DockLayoutLayer)
