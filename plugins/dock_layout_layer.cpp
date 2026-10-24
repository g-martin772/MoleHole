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

    std::optional<std::string> ReadText(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    void DockAll(const ImGuiID target, const std::initializer_list<const char*> titles)
    {
        for (const char* title : titles) ImGui::DockBuilderDockWindow(title, target);
    }

    void BuildDock(const LayoutKind kind, const ImGuiID dockspaceId)
    {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

        ImGuiID right, center;
        ImGuiID rightTop, rightBottom, bottom;
        switch (kind)
        {
        case LayoutKind::Edit:
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Right, 0.28f, &right, &center);
            ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.5f, &rightTop, &rightBottom);
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, &bottom, &center);
            DockAll(rightTop, {"System", "Camera"});
            DockAll(rightBottom, {"Scene", "Render", "Simulation", "Undo History"});
            DockAll(center, {"Viewport"});
            DockAll(bottom, {"General Relativity", "Science", "Log", "Profiler", "Animation Graph"});
            break;
        case LayoutKind::Render:
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Right, 0.30f, &right, &center);
            ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.62f, &rightTop, &rightBottom);
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, &bottom, &center);
            DockAll(rightTop, {"Render"});
            DockAll(rightBottom, {"Camera", "System", "General Relativity", "Simulation", "Scene", "Undo History"});
            DockAll(center, {"Viewport"});
            DockAll(bottom, {"Log", "Profiler", "Science", "Animation Graph"});
            break;
        case LayoutKind::Graph:
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Right, 0.22f, &right, &center);
            ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.55f, &rightTop, &rightBottom);
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.58f, &bottom, &center);
            DockAll(rightTop, {"Scene", "Camera"});
            DockAll(rightBottom, {"Simulation", "Log", "Render", "System", "Undo History"});
            DockAll(center, {"Viewport"});
            DockAll(bottom, {"Animation Graph", "General Relativity", "Science", "Profiler"});
            break;
        }
        ImGui::DockBuilderFinish(dockspaceId);
    }

    struct DockLayoutLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, Renderer, AppPaths, UiState>;

        DockLayoutLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<Renderer> renderer,
                        std::shared_ptr<AppPaths> appPaths, std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_Renderer(std::move(renderer)), m_AppPaths(std::move(appPaths)),
              m_UiState(std::move(uiState))
        {
        }

        void OnUiRender() override
        {
            // First UI layer of the main window: give every later window the Unicode fallback glyphs
            EnsureSymbolFallback(m_AppPaths->ResourceDir() / "font" / "Roboto-Regular.ttf");
            if (!m_Done)
            {
                m_Done = true;
                ApplyFirstRunLayout();
            }
            HandleRequests();
        }

    private:
        [[nodiscard]] std::filesystem::path LayoutDir() const { return m_AppPaths->ConfigDir() / "layouts"; }

        void Notify(const NotificationKind kind, const std::string& title, const std::string& body = {})
        {
            m_Logger->Notifications()->Post(kind, title, body, 0.0f, "layout");
        }

        void ApplyFirstRunLayout()
        {
            if (HasDockLayout(m_AppPaths->ConfigDir() / "imgui_main.ini"))
            {
                m_Logger->Info("DockLayoutLayer: existing layout found, leaving it alone");
                return;
            }
            if (const auto dockspaceId = m_Renderer->GetMainDockspaceId(); dockspaceId != 0)
            {
                BuildDock(LayoutKind::Edit, dockspaceId);
                m_Logger->Info("DockLayoutLayer: applied first-run docking layout");
            }
        }

        void HandleRequests()
        {
            for (const auto& request : m_UiState->Actions.Take({"layout."}))
            {
                if (request.Id == "layout.reset") Reset();
                else if (request.Id == "layout.apply") Apply(request.Arg);
                else if (request.Id == "layout.write") Write(request.Arg);
                else if (request.Id == "layout.delete") Delete(request.Arg);
            }
        }

        void Reset()
        {
            const auto dockspaceId = m_Renderer->GetMainDockspaceId();
            if (dockspaceId == 0) return;
            BuildDock(LayoutKind::Edit, dockspaceId);
            ImGui::MarkIniSettingsDirty();
            Notify(NotificationKind::Info, "Layout reset");
        }

        void Apply(const std::string& name)
        {
            const auto dockspaceId = m_Renderer->GetMainDockspaceId();
            if (dockspaceId == 0) return;
            if (const auto* builtIn = FindBuiltInLayout(name))
            {
                BuildDock(builtIn->Kind, dockspaceId);
                ApplyVisibleWindows(*m_UiState, std::span<const std::string_view>(builtIn->Windows));
                ImGui::MarkIniSettingsDirty();
                Notify(NotificationKind::Info, "Layout applied", name);
                return;
            }
            const auto text = ReadText(LayoutPath(LayoutDir(), name));
            if (!text || !text->contains("[Docking][Data]"))
            {
                Notify(NotificationKind::Error, "Layout not applied", std::format("'{}' is missing or unreadable", name));
                return;
            }
            m_Renderer->RunBeforeUiFrame([ui = m_UiState, text = *text]
            {
                ImGui::LoadIniSettingsFromMemory(text.c_str(), text.size());
                if (const auto visible = DecodeLayoutVisible(text)) ApplyVisibleWindows(*ui, std::span<const std::string>(*visible));
                ImGui::MarkIniSettingsDirty();
            });
            Notify(NotificationKind::Info, "Layout applied", name);
        }

        void Write(const std::string& rawName)
        {
            const auto name = SanitizeLayoutName(rawName);
            if (!name || IsReservedLayoutName(*name))
            {
                Notify(NotificationKind::Error, "Layout not saved", "Choose another name");
                return;
            }
            std::size_t size = 0;
            const char* ini = ImGui::SaveIniSettingsToMemory(&size);
            const auto visible = CollectVisibleWindows(*m_UiState);
            const auto text = EncodeLayout(std::string_view(ini, size), visible);
            if (const auto written = WriteFileAtomic(LayoutPath(LayoutDir(), *name), text); !written)
            {
                Notify(NotificationKind::Error, "Layout not saved", written.error());
                return;
            }
            Notify(NotificationKind::Info, "Layout saved", *name);
        }

        void Delete(const std::string& name)
        {
            if (name.empty() || IsReservedLayoutName(name)) return;
            std::error_code ec;
            if (std::filesystem::remove(LayoutPath(LayoutDir(), name), ec)) Notify(NotificationKind::Info, "Layout deleted", name);
            else Notify(NotificationKind::Error, "Layout not deleted", name);
        }

        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<AppPaths> m_AppPaths;
        std::shared_ptr<UiState> m_UiState;
        bool m_Done = false;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(DockLayoutLayer)
