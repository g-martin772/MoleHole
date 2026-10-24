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
        using Dependencies = std::tuple<Logger, FileDialog, UiState, AppStateService, IFileSystem, AppPaths, AppInfo, Renderer>;

        LauncherLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<FileDialog> fileDialog,
                      std::shared_ptr<UiState> uiState, std::shared_ptr<AppStateService> appState,
                      std::shared_ptr<IFileSystem> fileSystem, std::shared_ptr<AppPaths> appPaths,
                      std::shared_ptr<AppInfo> appInfo, std::shared_ptr<Renderer> renderer)
            : HotReloadableLayer(logger), m_FileDialog(std::move(fileDialog)), m_UiState(std::move(uiState)),
              m_AppState(std::move(appState)), m_FileSystem(std::move(fileSystem)), m_AppPaths(std::move(appPaths)),
              m_AppInfo(std::move(appInfo)), m_Renderer(std::move(renderer))
        {
        }

        void OnAttach() override
        {
            m_Data.Branding = {.Title = m_AppInfo->Name, .Tagline = m_AppInfo->Summary,
                               .Version = "Version " + m_AppInfo->Version};
            m_Data.ShowOnStartup = m_AppState->GetShowLauncher();
            m_Data.Templates = CollectTemplates();
            RefreshRecents();
            RefreshCrashNotice();
            RefreshRecoveryNotices();
            m_Hooks.DrawSettings = [this] { DrawSettings(); };
        }

        void OnDetach() override
        {
            m_Data.Branding.IconTexture = 0;
            m_Icon.reset();
            m_IconTried = false;
        }

        void OnUiRender() override
        {
            EnsureIcon();
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
        void EnsureIcon()
        {
            if (m_IconTried) return;
            m_IconTried = true;
            if (m_AppInfo->IconFile.empty()) return;

            const int size = static_cast<int>(std::lround(ImGui::GetFontSize() * 8.0f));
            const auto path = m_FileSystem->ResolvePath(m_AppInfo->IconFile);
            const auto image = RasterizeSvgFile(path, size, size);
            if (!image)
            {
                m_Logger->Warn("Launcher icon {} not loaded: {}", path.string(), image.error());
                return;
            }
            m_Icon = m_Renderer->CreateUiTexture(image->Rgba, image->Width, image->Height);
            if (m_Icon) m_Data.Branding.IconTexture = m_Icon->Id();
        }

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
                case LauncherAction::Rescan:
                    m_Data.Templates = CollectTemplates();
                    RefreshRecents();
                    RefreshCrashNotice();
                    RefreshRecoveryNotices();
                    break;
                case LauncherAction::Quit: Application::Instance().Stop(); break;
                case LauncherAction::Custom: HandleCustom(action.Path); break;
                default: break;
            }
        }

        void RefreshCrashNotice()
        {
            m_CrashReport = FindPendingCrashReport(m_AppPaths->LogDir());
            m_Data.Notice = {};
            if (!m_CrashReport) return;
            m_Data.Notice = {.Text = "Last run crashed", .ActionLabel = "Open report", .ActionId = "crash:open",
                             .DismissId = "crash:dismiss"};
        }

        void RefreshRecoveryNotices()
        {
            m_Recoveries = m_UiState->Recovery.Scan();
            m_Data.Notices.clear();
            const auto now = RecentList::Now();
            for (const auto& entry : m_Recoveries)
            {
                const auto name = entry.ScenePath.empty() ? entry.SceneName : std::filesystem::path(entry.ScenePath).stem().string();
                m_Data.Notices.push_back({std::format("Recover unsaved changes: {} ({})", name, RelativeTimeText(entry.Modified, now)),
                                          "Restore", "recover:restore:" + entry.File.string(),
                                          "recover:discard:" + entry.File.string(), "Discard"});
            }
        }

        bool HandleRecoveryAction(const std::string& id)
        {
            constexpr std::string_view kRestore = "recover:restore:";
            constexpr std::string_view kDiscard = "recover:discard:";
            const bool restore = id.starts_with(kRestore);
            if (!restore && !id.starts_with(kDiscard)) return false;
            const std::filesystem::path file = id.substr(restore ? kRestore.size() : kDiscard.size());
            if (restore) m_UiState->RequestRelaunch({"--recover", file.string(), "--skip-intro"});
            else
            {
                RecoveryStore::Remove(file);
                RefreshRecoveryNotices();
            }
            return true;
        }

        void HandleCustom(const std::string& id)
        {
            if (HandleRecoveryAction(id)) return;
            if (!m_CrashReport) return;
            if (id == "crash:open")
            {
                if (const auto opened = OpenPath(*m_CrashReport); !opened)
                {
                    m_Logger->Warn("Could not open crash report {}: {}", m_CrashReport->string(), opened.error());
                }
            }
            else if (id == "crash:dismiss")
            {
                MarkCrashReportSeen(*m_CrashReport);
                RefreshCrashNotice();
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
        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<ImGuiTextureHandle> m_Icon;
        bool m_IconTried = false;
        LauncherData m_Data;
        LauncherHooks m_Hooks;
        std::optional<std::filesystem::path> m_CrashReport;
        std::vector<RecoveryEntry> m_Recoveries;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(LauncherLayer)
