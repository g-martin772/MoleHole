import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct TopBarLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, FileDialog, SceneManager, UiPreferences, FontAssetCatalog, UiState,
                                        AppStateService, IFileSystem>;

        TopBarLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<FileDialog> fileDialog,
                   std::shared_ptr<SceneManager> scenes, std::shared_ptr<UiPreferences> uiPreferences,
                   std::shared_ptr<FontAssetCatalog> fontAssets, std::shared_ptr<UiState> uiState,
                   std::shared_ptr<AppStateService> appState, std::shared_ptr<IFileSystem> fileSystem)
            : HotReloadableLayer(logger), m_FileDialog(std::move(fileDialog)), m_Scenes(std::move(scenes)),
              m_UiPreferences(std::move(uiPreferences)), m_FontAssets(std::move(fontAssets)),
              m_UiState(std::move(uiState)), m_AppState(std::move(appState)), m_FileSystem(std::move(fileSystem))
        {
        }

        void OnUiRender() override
        {
            HandleShortcuts();
            RenderMenuBar();
            RenderSettingsPopup();
            RenderExportDialog();
        }

    private:
        struct TemplateEntry { std::string Label; std::string Path; };

        void HandleShortcuts()
        {
            if (ImGui::GetIO().WantTextInput) return;
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) DoSaveAs();
            else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) DoSave();
            else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) DoOpen();
            else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) DoNewScene();
        }

        void DoNewScene()
        {
            m_UiState->PendingNewScene = true;
        }

        void DoOpen()
        {
            if (const auto path = m_FileDialog->OpenFile({FileDialogFilter{"Scene", "yaml"}}))
            {
                m_UiState->PendingLoadScenePath.Set(path->string());
            }
        }

        void DoSave()
        {
            if (m_UiState->CurrentScenePath.empty())
            {
                DoSaveAs();
                return;
            }
            SaveLiveSceneTo(m_UiState->CurrentScenePath);
        }

        void DoSaveAs()
        {
            const auto defaultName = m_UiState->CurrentSceneName.empty()
                                         ? std::string("scene.yaml")
                                         : m_UiState->CurrentSceneName + ".yaml";
            if (const auto path = m_FileDialog->SaveFile({FileDialogFilter{"Scene", "yaml"}}, defaultName))
            {
                SaveLiveSceneTo(*path);
            }
        }

        void SaveLiveSceneTo(const std::filesystem::path& path)
        {
            try
            {
                const auto runner = m_Scenes->GetSimulation(m_UiState->CurrentSceneName);
                if (!runner)
                {
                    m_Logger->Error("Failed to save scene: no active simulation for '{}'",
                                   m_UiState->CurrentSceneName);
                    return;
                }
                std::string yaml;
                {
                    auto sceneLock = runner->LockRenderScene();
                    yaml = sceneLock->SerializeToYaml();
                }
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                if (!file)
                {
                    m_Logger->Error("Failed to save scene: could not open '{}' for writing", path.string());
                    return;
                }
                file << yaml;
                m_UiState->CurrentScenePath = path.string();
                m_Logger->Info("Scene saved to '{}'", m_UiState->CurrentScenePath);
                m_AppState->NotifySceneOpened(m_UiState->CurrentScenePath);
            }
            catch (const std::exception& error)
            {
                m_Logger->Error("Failed to save scene: {}", error.what());
            }
        }

        [[nodiscard]] std::vector<TemplateEntry> CollectTemplatePaths() const
        {
            std::vector<TemplateEntry> result;
            std::error_code ec;
            const auto dir = m_FileSystem->ResolvePath("templates");
            if (!std::filesystem::exists(dir, ec)) return result;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (ec) break;
                if (entry.path().extension() != ".yaml") continue;
                result.push_back({entry.path().stem().string(), entry.path().string()});
            }
            std::ranges::sort(result, {}, &TemplateEntry::Label);
            return result;
        }

        void RenderMenuBar()
        {
            if (!ImGui::BeginMainMenuBar()) return;

            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Scene", "Ctrl+N"))
                {
                    DoNewScene();
                }
                if (ImGui::BeginMenu("New from Template"))
                {
                    const auto templates = CollectTemplatePaths();
                    if (templates.empty())
                    {
                        ImGui::TextDisabled("(no templates found)");
                    }
                    for (const auto& entry : templates)
                    {
                        if (ImGui::MenuItem(entry.Label.c_str()))
                        {
                            m_UiState->PendingLoadTemplatePath.Set(entry.Path);
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
                {
                    DoOpen();
                }
                const auto recentScenes = m_AppState->GetRecentScenes();
                if (ImGui::BeginMenu("Open Recent", !recentScenes.empty()))
                {
                    for (const auto& scene : recentScenes)
                    {
                        if (ImGui::MenuItem(scene.c_str()))
                        {
                            m_UiState->PendingLoadScenePath.Set(scene);
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                {
                    DoSave();
                }
                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
                {
                    DoSaveAs();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Camera", nullptr, &m_UiState->ShowCameraWindow);
                ImGui::MenuItem("System", nullptr, &m_UiState->ShowSystemWindow);
                ImGui::MenuItem("Scene", nullptr, &m_UiState->ShowSceneWindow);
                ImGui::MenuItem("Debug", nullptr, &m_UiState->ShowDebugWindow);
                ImGui::MenuItem("Viewport HUD", nullptr, &m_UiState->ShowViewportHud);
                ImGui::MenuItem("Camera Gizmos", nullptr, &m_UiState->ShowCameraGizmos);
                ImGui::MenuItem("General Relativity", nullptr, &m_UiState->ShowGeneralRelativityWindow);
                ImGui::MenuItem("Science", nullptr, &m_UiState->ShowScienceWindow);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Export"))
            {
                if (ImGui::MenuItem("Export Render..."))
                {
                    m_ShowExportDialog = true;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("Start Tutorial"))
                {
                    StartTutorial(*m_UiState);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("About"))
                {
                    m_UiState->ShowSettingsWindow = true;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        void RenderSettingsPopup()
        {
            if (m_UiState->ShowSettingsWindow && !ImGui::IsPopupOpen("Settings"))
            {
                ImGui::OpenPopup("Settings");
            }

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(480, 450), ImGuiCond_Appearing);

            if (ImGui::BeginPopupModal("Settings", &m_UiState->ShowSettingsWindow, ImGuiWindowFlags_NoResize))
            {
                SectionHeader("MOLEHOLE");
                ImGui::TextWrapped(
                    "A relativistic black hole visualizer -- gravitational lensing, accretion disks, "
                    "and spacetime curvature rendered in real time.");

                SectionHeader("INTERFACE");
                ImGui::Checkbox("Play intro animation on startup", &m_UiState->IntroEnabled);
                float scale = m_UiPreferences->GetUiScale();
                if (ImGui::SliderFloat("UI Scale", &scale, 0.5f, 2.0f, "%.2f"))
                {
                    m_UiPreferences->SetUiScale(scale);
                }

                const auto currentFont = m_UiPreferences->GetFontName();
                const auto currentSize = m_UiPreferences->GetFontSize();
                const auto availableFonts = m_FontAssets->GetAvailableFonts();
                if (ImGui::BeginCombo("Font", currentFont.c_str()))
                {
                    for (const auto& font : availableFonts)
                    {
                        const bool isSelected = font.Name == currentFont;
                        if (ImGui::Selectable(font.Name.c_str(), isSelected))
                        {
                            m_UiPreferences->SetFont(font.Name, currentSize);
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                float fontSize = currentSize;
                if (ImGui::SliderFloat("Font Size", &fontSize, 10.0f, 32.0f, "%.0f"))
                {
                    m_UiPreferences->SetFont(currentFont, fontSize);
                }

                SectionHeader("EXPORT");
                {
                    std::array<char, 512> exportDir{};
                    const auto current = m_AppState->GetDefaultExportDirectory();
                    std::ranges::copy(current.substr(0, exportDir.size() - 1), exportDir.begin());
                    if (ImGui::InputText("Default Folder", exportDir.data(), exportDir.size()))
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
                    if (ImGui::Button("Reset##ExportFolder")) m_AppState->SetDefaultExportDirectory(".gpp/exports");
                    ImGui::TextDisabled("Relative paths start at the working directory");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGui::Button("Close", ImVec2(120, 0)))
                {
                    m_UiState->ShowSettingsWindow = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        void RenderExportDialog()
        {
            if (m_ShowExportDialog && !ImGui::IsPopupOpen("Export Render"))
            {
                ImGui::OpenPopup("Export Render");
            }

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);

            if (!ImGui::BeginPopupModal("Export Render", &m_ShowExportDialog, ImGuiWindowFlags_AlwaysAutoResize))
            {
                return;
            }

            const bool exporting = m_UiState->ExportActive;

            ImGui::BeginDisabled(exporting);

            SectionHeader("FORMAT");
            ImGui::RadioButton("Image (PNG)", &m_ExportKind, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Video (MP4)", &m_ExportKind, 1);

            SectionHeader("RESOLUTION");
            ImGui::InputInt("Width", &m_ExportWidth);
            ImGui::InputInt("Height", &m_ExportHeight);
            if (ImGui::Button("1280x720")) { m_ExportWidth = 1280; m_ExportHeight = 720; }
            ImGui::SameLine();
            if (ImGui::Button("1920x1080")) { m_ExportWidth = 1920; m_ExportHeight = 1080; }
            ImGui::SameLine();
            if (ImGui::Button("3840x2160")) { m_ExportWidth = 3840; m_ExportHeight = 2160; }
            m_ExportWidth = std::max(1, m_ExportWidth);
            m_ExportHeight = std::max(1, m_ExportHeight);

            if (m_ExportKind == 1)
            {
                SectionHeader("VIDEO");
                ImGui::InputFloat("Duration (s)", &m_ExportDuration);
                ImGui::InputInt("Framerate", &m_ExportFps);
                m_ExportDuration = std::max(0.1f, m_ExportDuration);
                m_ExportFps = std::clamp(m_ExportFps, 1, 240);
            }

            SectionHeader("QUALITY");
            ImGui::Checkbox("Override ray-march quality", &m_ExportOverrideQuality);
            if (m_ExportOverrideQuality)
            {
                ImGui::SliderFloat("Ray Step Size", &m_ExportRayStepSize, 0.001f, 0.05f, "%.4f");
                ImGui::InputInt("Max Ray Steps", &m_ExportMaxRaySteps);
                m_ExportMaxRaySteps = std::max(1000, m_ExportMaxRaySteps);
            }

            SectionHeader("OUTPUT");
            ImGui::InputTextWithHint("##ExportPath", "Auto-named in the default export folder", m_ExportPathBuffer.data(),
                                     m_ExportPathBuffer.size());
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
            {
                const auto defaultName = m_ExportKind == 0 ? "render.png" : "render.mp4";
                const auto filter = m_ExportKind == 0 ? FileDialogFilter{"PNG Image", "png"}
                                                       : FileDialogFilter{"MP4 Video", "mp4"};
                if (const auto path = m_FileDialog->SaveFile({filter}, defaultName, m_AppState->GetLastExportDirectory()))
                {
                    const auto str = path->string();
                    std::ranges::fill(m_ExportPathBuffer, '\0');
                    const auto count = std::min(str.size(), m_ExportPathBuffer.size() - 1);
                    std::ranges::copy(str.substr(0, count), m_ExportPathBuffer.begin());
                }
            }

            if (m_ExportPathBuffer[0] == '\0')
            {
                const auto next = m_AppState->NextExportPath(m_ExportKind == 0 ? ExportRequest::Kind::Image
                                                                                : ExportRequest::Kind::Video);
                ImGui::TextDisabled("Saves to %s", next.string().c_str());
            }

            ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (exporting)
            {
                ImGui::TextUnformatted(m_UiState->ExportStatus.c_str());
                ImGui::ProgressBar(m_UiState->ExportProgress);
            }
            else
            {
                if (ImGui::Button("Start Export", ImVec2(120, 0)))
                {
                    ExportRequest request;
                    request.RequestKind =
                        m_ExportKind == 0 ? ExportRequest::Kind::Image : ExportRequest::Kind::Video;
                    request.OutputPath = m_ExportPathBuffer[0] != '\0' ? std::string(m_ExportPathBuffer.data())
                                                                       : m_AppState->NextExportPath(request.RequestKind).string();
                    request.Width = static_cast<std::uint32_t>(m_ExportWidth);
                    request.Height = static_cast<std::uint32_t>(m_ExportHeight);
                    request.DurationSeconds = m_ExportDuration;
                    request.Framerate = m_ExportFps;
                    if (m_ExportOverrideQuality)
                    {
                        request.RayStepSize = m_ExportRayStepSize;
                        request.MaxRaySteps = m_ExportMaxRaySteps;
                    }
                    m_UiState->PendingExport.Set(request);
                    m_ShowExportDialog = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Close", ImVec2(120, 0)))
                {
                    m_ShowExportDialog = false;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::EndPopup();
        }

        std::shared_ptr<FileDialog> m_FileDialog;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiPreferences> m_UiPreferences;
        std::shared_ptr<FontAssetCatalog> m_FontAssets;
        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<AppStateService> m_AppState;

        bool m_ShowExportDialog = false;
        int m_ExportKind = 0; // 0 = Image, 1 = Video
        int m_ExportWidth = 1920;
        int m_ExportHeight = 1080;
        float m_ExportDuration = 5.0f;
        int m_ExportFps = 30;
        bool m_ExportOverrideQuality = false;
        float m_ExportRayStepSize = 0.01f;
        int m_ExportMaxRaySteps = 100000;
        std::array<char, 512> m_ExportPathBuffer{};
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(TopBarLayer)
