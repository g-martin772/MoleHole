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
    bool HasDockLayout(const std::filesystem::path& iniPath)
    {
        std::ifstream in(iniPath);
        if (!in) return false;
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return text.find("[Docking][Data]") != std::string::npos && text.find("DockSpace") != std::string::npos;
    }

    struct DockLayoutLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, Renderer, AppPaths>;

        DockLayoutLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<Renderer> renderer,
                        std::shared_ptr<AppPaths> appPaths)
            : HotReloadableLayer(logger), m_Renderer(std::move(renderer)), m_AppPaths(std::move(appPaths))
        {
        }

        void OnUiRender() override
        {
            // First UI layer of the main window: give every later window the Unicode fallback glyphs
            EnsureSymbolFallback(m_AppPaths->ResourceDir() / "font" / "Roboto-Regular.ttf");
            if (m_Done) return;
            m_Done = true;

            if (HasDockLayout(m_AppPaths->ConfigDir() / "imgui_main.ini"))
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
            ImGui::DockBuilderDockWindow("Render", dockRightBottom);
            ImGui::DockBuilderDockWindow("Simulation", dockRightBottom);
            ImGui::DockBuilderDockWindow("Viewport", dockCenter);
            ImGui::DockBuilderDockWindow("General Relativity", dockBottom);
            ImGui::DockBuilderDockWindow("Science", dockBottom);
            ImGui::DockBuilderDockWindow("Log", dockBottom);

            ImGui::DockBuilderFinish(dockspaceId);
            m_Logger->Info("DockLayoutLayer: applied first-run docking layout");
        }

    private:
        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<AppPaths> m_AppPaths;
        bool m_Done = false;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(DockLayoutLayer)
