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
                                        AppStateService, IFileSystem, RenderSettingsService, AppPaths, WindowManager,
                                        AppInfo>;

        TopBarLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<FileDialog> fileDialog,
                   std::shared_ptr<SceneManager> scenes, std::shared_ptr<UiPreferences> uiPreferences,
                   std::shared_ptr<FontAssetCatalog> fontAssets, std::shared_ptr<UiState> uiState,
                   std::shared_ptr<AppStateService> appState, std::shared_ptr<IFileSystem> fileSystem,
                   std::shared_ptr<RenderSettingsService> renderSettings, std::shared_ptr<AppPaths> appPaths,
                   std::shared_ptr<WindowManager> windows, std::shared_ptr<AppInfo> appInfo)
            : HotReloadableLayer(logger), m_FileDialog(std::move(fileDialog)), m_Scenes(std::move(scenes)),
              m_UiPreferences(std::move(uiPreferences)), m_FontAssets(std::move(fontAssets)),
              m_UiState(std::move(uiState)), m_AppState(std::move(appState)), m_FileSystem(std::move(fileSystem)),
              m_RenderSettings(std::move(renderSettings)), m_AppPaths(std::move(appPaths)), m_Windows(std::move(windows)),
              m_AppInfo(std::move(appInfo))
        {
        }

        void OnAttach() override
        {
            m_Windows->SetCloseGuard([state = m_UiState]
            {
                if (!state->SceneState.IsDirty()) return true;
                state->QuitRequested = true;
                return false;
            });
        }

        void OnDetach() override
        {
            m_Windows->SetCloseGuard({});
        }

        void OnUiRender() override
        {
            if (m_UiState->QuitRequested.exchange(false)) RequestAction({SceneAction::Kind::Quit, {}});
            HandleShortcuts();
            RenderMenuBar();
            RenderSettingsPopup();
            RenderExportDialog();
            RenderUnsavedPrompt();
            UpdateWindowTitle();
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

        void RequestAction(SceneAction action)
        {
            if (m_UiState->UnsavedChanges.Request(action, m_UiState->SceneState.IsDirty())) RunAction(action);
        }

        void RunAction(const SceneAction& action)
        {
            switch (action.What)
            {
            case SceneAction::Kind::NewScene: m_UiState->PendingNewScene = true; break;
            case SceneAction::Kind::OpenFile: m_UiState->PendingLoadScenePath.Set(action.Path); break;
            case SceneAction::Kind::OpenTemplate: m_UiState->PendingLoadTemplatePath.Set(action.Path); break;
            case SceneAction::Kind::CloseToLauncher: m_UiState->RequestRelaunch({"--launcher"}); break;
            case SceneAction::Kind::Quit: Application::Instance().Stop(); break;
            case SceneAction::Kind::None: break;
            }
        }

        void DoNewScene()
        {
            RequestAction({SceneAction::Kind::NewScene, {}});
        }

        [[nodiscard]] std::string DisplayName() const
        {
            if (!m_UiState->CurrentScenePath.empty())
                return std::filesystem::path(m_UiState->CurrentScenePath).stem().string();
            return m_UiState->CurrentSceneName;
        }

        void UpdateWindowTitle()
        {
            const auto name = DisplayName();
            auto title = name.empty() ? m_AppInfo->Name
                                      : std::format("{}{} - {}", name, m_UiState->SceneState.IsDirty() ? "*" : "", m_AppInfo->Name);
            if (title == m_WindowTitle) return;
            m_WindowTitle = title;
            m_Windows->SetMainWindowTitle(std::move(title));
        }

        [[nodiscard]] static const char* ActionVerb(const SceneAction::Kind kind)
        {
            switch (kind)
            {
            case SceneAction::Kind::NewScene: return "creating a new scene";
            case SceneAction::Kind::OpenFile:
            case SceneAction::Kind::OpenTemplate: return "opening another scene";
            case SceneAction::Kind::CloseToLauncher: return "closing the scene";
            case SceneAction::Kind::Quit: return "quitting";
            case SceneAction::Kind::None: break;
            }
            return "continuing";
        }

        void RenderUnsavedPrompt()
        {
            auto& prompt = m_UiState->UnsavedChanges;
            if (!prompt.Open())
            {
                m_PromptFrames = 0;
                return;
            }
            if (!ImGui::IsPopupOpen("Unsaved Changes")) ImGui::OpenPopup("Unsaved Changes");

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr,
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
            {
                return;
            }

            ImGui::Text("\"%s\" has unsaved changes.", DisplayName().c_str());
            ImGui::TextDisabled("Save them before %s?", ActionVerb(prompt.Pending().What));
            ImGui::Spacing();

            std::optional<UnsavedChoice> choice;
            if (ImGui::Button("Save", ImVec2(110, 0))) choice = UnsavedChoice::Save;
            if (m_PromptFrames == 0) ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(110, 0))) choice = UnsavedChoice::Discard;
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110, 0))) choice = UnsavedChoice::Cancel;
            ImGui::TextDisabled("Enter = Save   Esc = Cancel");

            if (m_PromptFrames++ > 0 && !choice)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
                    choice = UnsavedChoice::Save;
                else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) choice = UnsavedChoice::Cancel;
            }

            if (choice)
            {
                const bool saved = *choice == UnsavedChoice::Save && SaveCurrent();
                const auto action = prompt.Resolve(*choice, saved);
                if (!prompt.Open()) ImGui::CloseCurrentPopup();
                if (action) RunAction(*action);
            }
            ImGui::EndPopup();
        }

        [[nodiscard]] std::filesystem::path ScenesDirectory() const
        {
            return m_AppPaths->IsInstalled() ? m_AppPaths->UserSubdir("scenes") : std::filesystem::path{};
        }

        void DoOpen()
        {
            if (const auto path = m_FileDialog->OpenFile({FileDialogFilter{"Scene", "yaml"}}, ScenesDirectory()))
            {
                RequestAction({SceneAction::Kind::OpenFile, path->string()});
            }
        }

        bool SaveCurrent()
        {
            return m_UiState->CurrentScenePath.empty() ? DoSaveAs() : SaveLiveSceneTo(m_UiState->CurrentScenePath);
        }

        void DoSave()
        {
            SaveCurrent();
        }

        bool DoSaveAs()
        {
            const auto defaultName = m_UiState->CurrentSceneName.empty()
                                         ? std::string("scene.yaml")
                                         : m_UiState->CurrentSceneName + ".yaml";
            if (const auto path = m_FileDialog->SaveFile({FileDialogFilter{"Scene", "yaml"}}, defaultName, ScenesDirectory()))
            {
                return SaveLiveSceneTo(*path);
            }
            return false;
        }

        void SaveFailed(const std::string& reason)
        {
            m_Logger->Error("Failed to save scene: {}", reason);
            m_Logger->Notifications()->Post(NotificationKind::Error, "Save failed", reason, 0.0f, "scene-save");
        }

        bool SaveLiveSceneTo(const std::filesystem::path& path)
        {
            try
            {
                const auto runner = m_Scenes->GetSimulation(m_UiState->CurrentSceneName);
                if (!runner)
                {
                    SaveFailed(std::format("no active simulation for '{}'", m_UiState->CurrentSceneName));
                    return false;
                }
                std::string yaml;
                std::uint64_t revision = 0;
                {
                    auto sceneLock = runner->LockRenderScene();
                    yaml = sceneLock->SerializeToYaml();
                    revision = sceneLock.ContentRevision();
                }
                if (const auto saved = SaveSceneFile(path, yaml); !saved)
                {
                    SaveFailed(saved.error());
                    return false;
                }
                const bool running = !runner->IsPaused();
                m_UiState->CurrentScenePath = path.string();
                m_UiState->SceneState.MarkSaved(revision, !running);
                m_Logger->Info("Scene saved to '{}'", m_UiState->CurrentScenePath);
                m_Logger->Notifications()->Post(NotificationKind::Success, "Scene saved",
                                                running ? path.string() + "\nSaved while running: this is the simulated state"
                                                        : path.string(),
                                                0.0f, "scene-save");
                m_AppState->NotifySceneOpened(m_UiState->CurrentScenePath);
                return true;
            }
            catch (const std::exception& error)
            {
                SaveFailed(error.what());
                return false;
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
                            RequestAction({SceneAction::Kind::OpenTemplate, entry.Path});
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
                            RequestAction({SceneAction::Kind::OpenFile, scene});
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
                ImGui::Separator();
                if (ImGui::MenuItem("Close Scene (Launcher)"))
                {
                    RequestAction({SceneAction::Kind::CloseToLauncher, {}});
                }
                if (ImGui::MenuItem("Launcher..."))
                {
                    if (const auto spawned = SpawnSelf(std::vector<std::string>{"--launcher"}); !spawned)
                    {
                        m_Logger->Error("Failed to open the launcher: {}", spawned.error());
                    }
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Open another scene in a new window");
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Camera", nullptr, &m_UiState->ShowCameraWindow);
                ImGui::MenuItem("System", nullptr, &m_UiState->ShowSystemWindow);
                ImGui::MenuItem("Profiler", nullptr, &m_UiState->ShowProfilerWindow);
                ImGui::MenuItem("Log", nullptr, &m_UiState->ShowLogWindow);
                ImGui::MenuItem("Scene", nullptr, &m_UiState->ShowSceneWindow);
                ImGui::MenuItem("Render", nullptr, &m_UiState->ShowRenderWindow);
                ImGui::MenuItem("Simulation", nullptr, &m_UiState->ShowSimulationWindow);
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
            ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_Appearing);

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

                SectionHeader("UNITS");
                {
                    const auto current = Units::GetDisplayPreset();
                    ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
                    if (ImGui::BeginCombo("Display units", std::string(Units::DisplayPresetName(current)).c_str()))
                    {
                        for (const auto preset : Units::kDisplayPresets)
                        {
                            if (ImGui::Selectable(std::string(Units::DisplayPresetName(preset)).c_str(), preset == current))
                            {
                                Units::SetDisplayPreset(preset);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Which units the inspector prefers for lengths, masses and times.\n"
                                          "Auto picks by magnitude; Human = m/km/kg, Stellar = Rsun/AU/Msun, Galactic = pc/Msun/yr.\n"
                                          "Right-click a value to force one unit for that field.");
                    }
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
                    if (ImGui::Button("Reset##ExportFolder")) m_AppState->SetDefaultExportDirectory(m_AppPaths->UserSubdir("exports").string());
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
            {
                const auto& set = m_RenderSettings->Profiles();
                const std::string defaultLabel = "Export context: " + set.ProfileFor(RenderContext::Export).Name;
                const char* preview = m_ExportProfile.empty() ? defaultLabel.c_str() : m_ExportProfile.c_str();
                ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
                if (ImGui::BeginCombo("Render profile", preview))
                {
                    if (ImGui::Selectable(defaultLabel.c_str(), m_ExportProfile.empty())) m_ExportProfile.clear();
                    for (const auto& profile : set.Profiles())
                    {
                        if (ImGui::Selectable(profile.Name.c_str(), profile.Name == m_ExportProfile))
                            m_ExportProfile = profile.Name;
                        if (ImGui::IsItemHovered() && !profile.Description.empty())
                            ImGui::SetTooltip("%s", profile.Description.c_str());
                    }
                    ImGui::EndCombo();
                }
                if (m_ExportProfile.empty() || !set.Find(m_ExportProfile)) m_ExportProfile.clear();
            }
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
                    if (!m_ExportProfile.empty()) request.Profile = m_ExportProfile;
                    if (m_ExportOverrideQuality)
                    {
                        request.Overrides.emplace_back("transport.step_size", std::format("{}", m_ExportRayStepSize));
                        request.Overrides.emplace_back("transport.max_steps", std::format("{}", m_ExportMaxRaySteps));
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
        std::shared_ptr<RenderSettingsService> m_RenderSettings;
        std::shared_ptr<AppPaths> m_AppPaths;

        std::shared_ptr<WindowManager> m_Windows;
        std::shared_ptr<AppInfo> m_AppInfo;
        std::string m_WindowTitle;
        int m_PromptFrames = 0;
        std::string m_ExportProfile;
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
