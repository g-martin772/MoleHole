#include <imgui.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct LauncherLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, FileDialog, UiState, AppStateService, IFileSystem, AppPaths, AppInfo>;

        LauncherLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<FileDialog> fileDialog,
                      std::shared_ptr<UiState> uiState, std::shared_ptr<AppStateService> appState,
                      std::shared_ptr<IFileSystem> fileSystem, std::shared_ptr<AppPaths> appPaths,
                      std::shared_ptr<AppInfo> appInfo)
            : HotReloadableLayer(logger), m_FileDialog(std::move(fileDialog)), m_UiState(std::move(uiState)),
              m_AppState(std::move(appState)), m_FileSystem(std::move(fileSystem)), m_AppPaths(std::move(appPaths)),
              m_AppInfo(std::move(appInfo))
        {
        }

        void OnAttach() override
        {
            m_Data.Branding = {.Title = m_AppInfo->Name, .Tagline = m_AppInfo->Summary,
                               .Version = "Version " + m_AppInfo->Version};
            m_Data.ShowOnStartup = m_AppState->GetShowLauncher();
            m_Data.Templates = CollectTemplates();
            RefreshRecents();
            m_Hooks.DrawSettings = [this] { DrawSettings(); };
        }

        void OnUiRender() override
        {
            EnsureSymbolFallback(m_AppPaths->ResourceDir() / "font" / "Roboto-Regular.ttf");

            const auto* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_NoDocking;
            LauncherAction action;
            if (ImGui::Begin("##launcher", nullptr, flags))
            {
                action = DrawLauncher(m_Data, m_Hooks);
            }
            ImGui::End();
            ImGui::PopStyleVar(2);

            if (m_Data.ShowOnStartup != m_AppState->GetShowLauncher()) m_AppState->SetShowLauncher(m_Data.ShowOnStartup);
            Handle(action);
        }

    private:
        void Handle(const LauncherAction& action)
        {
            switch (action.Kind)
            {
                case LauncherAction::OpenRecent: OpenScene(action.Path); break;
                case LauncherAction::NewItem: m_UiState->RequestRelaunch({"--new", "--skip-intro"}); break;
                case LauncherAction::OpenFile:
                    if (const auto path = m_FileDialog->OpenFile({FileDialogFilter{"Scene", "yaml"}}, ScenesDirectory()))
                    {
                        OpenScene(path->string());
                    }
                    break;
                case LauncherAction::OpenTemplate:
                    m_UiState->RequestRelaunch({"--template", action.Path, "--skip-intro"});
                    break;
                case LauncherAction::RemoveRecent:
                    m_AppState->RemoveRecent(action.Path);
                    RefreshRecents();
                    break;
                case LauncherAction::Quit: Application::Instance().Stop(); break;
                default: break;
            }
        }

        void OpenScene(const std::string& path)
        {
            m_UiState->RequestRelaunch({"--scene", path, "--skip-intro"});
        }

        [[nodiscard]] std::filesystem::path ScenesDirectory() const
        {
            if (m_AppPaths->IsInstalled()) return m_AppPaths->UserSubdir("scenes");
            const auto recents = m_AppState->GetRecents();
            return recents.empty() ? std::filesystem::path{} : std::filesystem::path(recents.front().Path).parent_path();
        }

        void RefreshRecents()
        {
            m_Data.Recents.clear();
            const auto now = RecentList::Now();
            for (const auto& entry : m_AppState->GetRecents())
            {
                m_Data.Recents.push_back({entry.Path, entry.DisplayName(), RelativeTimeText(entry.LastOpened, now)});
            }
        }

        [[nodiscard]] std::vector<LauncherTemplate> CollectTemplates() const
        {
            std::vector<LauncherTemplate> result;
            std::error_code error;
            const auto directory = m_FileSystem->ResolvePath("templates");
            for (const auto& entry : std::filesystem::directory_iterator(directory, error))
            {
                if (error) break;
                if (entry.path().extension() != ".yaml") continue;
                result.push_back({entry.path().stem().string(), entry.path().string(),
                                  "templates/" + entry.path().filename().string()});
            }
            std::ranges::sort(result, {}, &LauncherTemplate::Name);
            return result;
        }

        void DrawSettings()
        {
            ImGui::Checkbox("Play intro animation on startup", &m_UiState->IntroEnabled);
            ImGui::Spacing();
            ImGui::TextUnformatted("Default export folder");
            std::array<char, 512> exportDir{};
            const auto current = m_AppState->GetDefaultExportDirectory();
            std::ranges::copy(current.substr(0, exportDir.size() - 1), exportDir.begin());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##ExportFolder", exportDir.data(), exportDir.size()))
            {
                m_AppState->SetDefaultExportDirectory(exportDir.data());
            }
            if (ImGui::Button("Browse...##ExportFolder"))
            {
                if (const auto folder = m_FileDialog->PickFolder(m_FileSystem->ResolvePath(current)))
                {
                    m_AppState->SetDefaultExportDirectory(folder->string());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset##ExportFolder"))
            {
                m_AppState->SetDefaultExportDirectory(m_AppPaths->UserSubdir("exports").string());
            }
        }

        std::shared_ptr<FileDialog> m_FileDialog;
        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<AppStateService> m_AppState;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<AppPaths> m_AppPaths;
        std::shared_ptr<AppInfo> m_AppInfo;
        LauncherData m_Data;
        LauncherHooks m_Hooks;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(LauncherLayer)
