module;
#include <yaml-cpp/yaml.h>
module MoleHole;

import :Application.State;
import :UI.TutorialState;
import std;
import glm;
import GPP;

using namespace GPP;

namespace MoleHole
{
    namespace
    {
        bool HasLegacyRenderKeys(const YAML::Node& render)
        {
            return render && (render["GravitationalLensing"] || render["BloomEnabled"] || render["MetricType"]
                              || render["PhysicallyAccurate"]);
        }
    }

    AppStateService::AppStateService(std::shared_ptr<UiState> uiState, std::shared_ptr<RenderSettingsService> renderSettings,
                                      std::shared_ptr<UiPreferences> preferences,
                                      std::shared_ptr<IFileSystem> fileSystem, std::shared_ptr<AppPaths> appPaths,
                                      std::shared_ptr<Logger> logger)
        : m_UiState(std::move(uiState)), m_RenderSettings(std::move(renderSettings)), m_Preferences(std::move(preferences)),
          m_FileSystem(std::move(fileSystem)), m_AppPaths(std::move(appPaths)), m_Logger(std::move(logger)),
          m_DefaultExportDirectory(m_AppPaths->UserSubdir("exports").string())
    {
    }

    Task<void> AppStateService::StartAsync(std::stop_token)
    {
        m_StatePath = m_FileSystem->ResolvePath(m_AppPaths->ConfigDir() / "molehole_state.yaml", PathAnchor::WorkingDir);
        m_Persist = !m_UiState->ExitWhenExportDone;
        Load();
        if (m_UiState->LauncherMode)
        {
            m_UiState->CurrentScenePath = m_LastScenePath;
            m_UiState->IntroActive = false;
        }
        else if (m_Persist)
        {
            if (!m_UiState->IntroEnabled) m_UiState->IntroActive = false;
            if (!m_TutorialCompleted)
            {
                StartTutorial(*m_UiState);
            }
        }
        co_return;
    }

    Task<void> AppStateService::StopAsync()
    {
        if (m_Persist && (m_UiState->DiscardOnExit || !m_UiState->SceneState.IsDirty())) m_UiState->Recovery.Clear();
        Save();
        co_return;
    }

    void AppStateService::Load()
    {
        if (!std::filesystem::exists(m_StatePath))
        {
            return;
        }

        try
        {
            const auto node = YAML::LoadFile(m_StatePath.string());

            if (const auto scene = node["LastScenePath"]; scene && scene.IsScalar())
            {
                m_LastScenePath = scene.as<std::string>();
            }
            m_Recents.FromYaml(node["RecentScenes"]);
            m_Recents.Prune();
            if (const auto v = node["ShowLauncher"]; v && v.IsScalar()) m_ShowLauncher = v.as<bool>(true);
            if (const auto camera = node["Camera"])
            {
                if (const auto v = camera["Position"])
                {
                    m_UiState->CameraPositionM = glm::dvec3(v.as<glm::vec3>()) * GPP::Units::Legacy::kLengthM;
                }
                if (const auto v = camera["Speed"]) m_UiState->CameraSpeedMs = v.as<double>() * GPP::Units::Legacy::kLengthM;
                if (const auto v = camera["PositionM"]) m_UiState->CameraPositionM = v.as<glm::dvec3>();
                if (const auto v = camera["Yaw"]) m_UiState->CameraYaw = v.as<float>();
                if (const auto v = camera["Pitch"]) m_UiState->CameraPitch = v.as<float>();
                if (const auto v = camera["SpeedMs"]) m_UiState->CameraSpeedMs = v.as<double>();
                if (const auto v = camera["MouseSensitivity"]) m_UiState->CameraMouseSensitivity = v.as<float>();
            }
            if (const auto ui = node["UI"])
            {
                std::string font = m_Preferences->GetFontName();
                float fontSize = m_Preferences->GetFontSize();
                if (const auto v = ui["Font"]) font = v.as<std::string>();
                if (const auto v = ui["FontSize"]) fontSize = v.as<float>();
                m_Preferences->SetFont(font, fontSize);
                if (const auto v = ui["UiScale"]) m_Preferences->SetUiScale(v.as<float>());
                if (const auto v = ui["ShowViewportHud"]) m_UiState->ShowViewportHud = v.as<bool>();
                if (const auto v = ui["IntroEnabled"]) m_UiState->IntroEnabled = v.as<bool>();
                if (const auto v = ui["ShowCameraWindow"]) m_UiState->ShowCameraWindow = v.as<bool>();
                if (const auto v = ui["ShowSystemWindow"]) m_UiState->ShowSystemWindow = v.as<bool>();
                if (const auto v = ui["ShowLogWindow"]) m_UiState->ShowLogWindow = v.as<bool>();
                if (const auto v = ui["ShowSceneWindow"]) m_UiState->ShowSceneWindow = v.as<bool>();
                if (const auto v = ui["ShowSimulationWindow"]) m_UiState->ShowSimulationWindow = v.as<bool>();
                if (const auto v = ui["ShowRenderWindow"]) m_UiState->ShowRenderWindow = v.as<bool>();
                if (const auto v = ui["ShowGeneralRelativityWindow"])
                    m_UiState->ShowGeneralRelativityWindow = v.as<bool>();
                if (const auto v = ui["ShowScienceWindow"]) m_UiState->ShowScienceWindow = v.as<bool>();
                if (const auto v = ui["DisplayUnits"])
                {
                    if (const auto preset = Units::DisplayPresetFromName(v.as<std::string>())) Units::SetDisplayPreset(*preset);
                }
                if (const auto v = ui["ShowAnimationGraphWindow"])
                    m_UiState->ShowAnimationGraphWindow = v.as<bool>();
            }
            if (const auto autosave = node["Autosave"])
            {
                if (const auto v = autosave["Enabled"]) m_UiState->AutosaveEnabled = v.as<bool>();
                if (const auto v = autosave["DelaySeconds"]) m_UiState->AutosaveDelaySeconds = std::max(v.as<int>(), 1);
            }
            if (const auto render = node["Render"])
            {
                if (const auto v = render["ShowCameraGizmos"]) m_UiState->ShowCameraGizmos = v.as<bool>();
            }
            if (const auto defaults = node["RenderDefaults"])
            {
                std::vector<std::string> warnings;
                m_RenderSettings->SetDefaults(RenderProfileSet::FromYaml(defaults, &warnings));
                for (const auto& warning : warnings) m_Logger->Warn("AppStateService: {}", warning);
            }
            else if (HasLegacyRenderKeys(node["Render"]))
            {
                RenderProfile legacy;
                legacy.Name = "Legacy";
                legacy.Description = "Render settings from before profiles existed.";
                legacy.Base = "Lensed";
                legacy.Settings = LegacyRenderSettings(node["Render"]);
                RenderProfileSet defaults;
                defaults.Add(std::move(legacy));
                for (const auto context : {RenderContext::Edit, RenderContext::Play, RenderContext::Export})
                {
                    defaults.Assign(context, "Legacy");
                }
                m_RenderSettings->SetDefaults(std::move(defaults));
                m_Logger->Info("AppStateService: migrated the render toggles of '{}' into the 'Legacy' profile",
                               m_StatePath.string());
            }
            if (const auto simulation = node["Simulation"])
            {
                if (const auto v = simulation["Precision"])
                {
                    if (const auto p = Units::PrecisionFromName(v.as<std::string>()); p && Units::CpuSupports(*p))
                        m_UiState->SimulationPrecision = *p;
                }
                if (const auto v = simulation["TickRate"])
                    m_UiState->SimulationTickRate = std::clamp(v.as<float>(), 1.0f, 2000.0f);
            }
            if (const auto v = node["DefaultExportDirectory"]; v && v.IsScalar() && !v.as<std::string>().empty())
            {
                m_DefaultExportDirectory = v.as<std::string>();
            }
            if (const auto v = node["LastExportDirectory"]; v && v.IsScalar())
            {
                m_LastExportDirectory = v.as<std::string>();
            }
            if (const auto v = node["TutorialCompleted"]; v && v.IsScalar())
            {
                m_TutorialCompleted = v.as<bool>();
            }
            if (const auto scanDirs = node["MeshScanDirectories"]; scanDirs && scanDirs.IsSequence())
            {
                for (const auto& entry : scanDirs)
                {
                    m_MeshScanDirectories.push_back(entry.as<std::string>());
                }
            }

            m_Logger->Info("AppStateService: loaded state from '{}'", m_StatePath.string());
        }
        catch (const std::exception& error)
        {
            m_Logger->Warn("AppStateService: failed to load '{}' ({}), using defaults",
                           m_StatePath.string(), error.what());
        }
    }

    void AppStateService::Save() const
    {
        if (!m_Persist)
        {
            return;
        }

        try
        {
            YAML::Node root;
            root["LastScenePath"] = m_UiState->CurrentScenePath;

            {
                std::scoped_lock lock(m_Mutex);
                RecentList onDisk(m_Recents.MaxEntries());
                try
                {
                    onDisk.FromYaml(YAML::LoadFile(m_StatePath.string())["RecentScenes"]);
                }
                catch (const std::exception&)
                {
                }
                auto merged = m_Recents;
                merged.Merge(onDisk, m_RemovedRecents);
                merged.Prune();
                root["RecentScenes"] = merged.ToYaml();
                root["ShowLauncher"] = m_ShowLauncher;
            }

            root["Camera"]["PositionM"] = m_UiState->CameraPositionM;
            root["Camera"]["Yaw"] = m_UiState->CameraYaw;
            root["Camera"]["Pitch"] = m_UiState->CameraPitch;
            root["Camera"]["SpeedMs"] = m_UiState->CameraSpeedMs;
            root["Camera"]["MouseSensitivity"] = m_UiState->CameraMouseSensitivity;

            root["UI"]["Font"] = m_Preferences->GetFontName();
            root["UI"]["FontSize"] = m_Preferences->GetFontSize();
            root["UI"]["UiScale"] = m_Preferences->GetUiScale();
            root["UI"]["ShowViewportHud"] = m_UiState->ShowViewportHud;
            root["UI"]["IntroEnabled"] = m_UiState->IntroEnabled;
            root["UI"]["ShowCameraWindow"] = m_UiState->ShowCameraWindow;
            root["UI"]["ShowSystemWindow"] = m_UiState->ShowSystemWindow;
            root["UI"]["ShowLogWindow"] = m_UiState->ShowLogWindow;
            root["UI"]["ShowSceneWindow"] = m_UiState->ShowSceneWindow;
            root["UI"]["ShowSimulationWindow"] = m_UiState->ShowSimulationWindow;
            root["UI"]["ShowRenderWindow"] = m_UiState->ShowRenderWindow;
            root["UI"]["ShowGeneralRelativityWindow"] = m_UiState->ShowGeneralRelativityWindow;
            root["UI"]["ShowScienceWindow"] = m_UiState->ShowScienceWindow;
            root["UI"]["DisplayUnits"] = std::string(Units::DisplayPresetName(Units::GetDisplayPreset()));
            root["UI"]["ShowAnimationGraphWindow"] = m_UiState->ShowAnimationGraphWindow;

            root["Autosave"]["Enabled"] = m_UiState->AutosaveEnabled;
            root["Autosave"]["DelaySeconds"] = m_UiState->AutosaveDelaySeconds;

            root["Render"]["ShowCameraGizmos"] = m_UiState->ShowCameraGizmos;
            root["RenderDefaults"] = m_RenderSettings->Defaults().ToYaml();

            root["Simulation"]["TickRate"] = m_UiState->SimulationTickRate;
            root["Simulation"]["Precision"] = std::string(Units::PrecisionName(m_UiState->SimulationPrecision));

            root["LastExportDirectory"] = GetLastExportDirectory();
            root["DefaultExportDirectory"] = GetDefaultExportDirectory();
            root["TutorialCompleted"] = GetTutorialCompleted();

            YAML::Node scanDirsNode;
            {
                std::scoped_lock lock(m_Mutex);
                for (const auto& dir : m_MeshScanDirectories)
                {
                    scanDirsNode.push_back(dir);
                }
            }
            root["MeshScanDirectories"] = scanDirsNode;

            auto backupPath = m_StatePath;
            backupPath += ".backup";
            std::error_code ec;
            if (std::filesystem::exists(m_StatePath))
            {
                std::filesystem::copy_file(m_StatePath, backupPath,
                                            std::filesystem::copy_options::overwrite_existing, ec);
            }

            YAML::Emitter out;
            out << root;
            if (const auto written = WriteFileAtomic(m_StatePath, out.c_str()); !written)
            {
                m_Logger->Error("AppStateService: failed to write '{}': {}", m_StatePath.string(), written.error());
                return;
            }
            m_Logger->Info("AppStateService: saved state to '{}'", m_StatePath.string());
        }
        catch (const std::exception& error)
        {
            m_Logger->Error("AppStateService: failed to save '{}': {}", m_StatePath.string(), error.what());
        }
    }

    std::string AppStateService::GetLastScenePath() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_LastScenePath;
    }

    std::vector<std::string> AppStateService::GetRecentScenes() const
    {
        std::vector<std::string> paths;
        for (const auto& entry : GetRecents()) paths.push_back(entry.Path);
        return paths;
    }

    std::vector<RecentEntry> AppStateService::GetRecents() const
    {
        std::scoped_lock lock(m_Mutex);
        auto entries = m_Recents.Entries();
        std::erase_if(entries, [](const RecentEntry& entry)
        {
            std::error_code error;
            return !std::filesystem::exists(entry.Path, error);
        });
        return entries;
    }

    void AppStateService::RemoveRecent(const std::string& path)
    {
        std::scoped_lock lock(m_Mutex);
        m_Recents.Remove(path);
        m_RemovedRecents.push_back(RecentList::Normalize(path));
    }

    bool AppStateService::GetShowLauncher() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_ShowLauncher;
    }

    void AppStateService::SetShowLauncher(const bool show)
    {
        std::scoped_lock lock(m_Mutex);
        m_ShowLauncher = show;
    }

    void AppStateService::NotifySceneOpened(const std::string& path)
    {
        if (path.empty())
        {
            return;
        }
        const auto absolute =
            m_FileSystem->ResolvePath(path, PathAnchor::WorkingDir).lexically_normal().string();
        std::scoped_lock lock(m_Mutex);
        m_LastScenePath = absolute;
        m_Recents.Add(absolute);
        std::erase(m_RemovedRecents, absolute);
    }

    std::filesystem::path NextNumberedPath(const std::filesystem::path& directory, const std::string_view stem,
                                           const std::string_view extension)
    {
        for (int index = 1;; ++index)
        {
            auto candidate = directory / std::format("{}_{:03d}.{}", stem, index, extension);
            if (!std::filesystem::exists(candidate)) return candidate;
        }
    }

    std::string AppStateService::GetDefaultExportDirectory() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_DefaultExportDirectory;
    }

    void AppStateService::SetDefaultExportDirectory(std::string directory)
    {
        std::scoped_lock lock(m_Mutex);
        m_DefaultExportDirectory = std::move(directory);
    }

    std::filesystem::path AppStateService::NextExportPath(const ExportRequest::Kind kind) const
    {
        auto directory = m_FileSystem->ResolvePath(GetDefaultExportDirectory());
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        const bool image = kind == ExportRequest::Kind::Image;
        return NextNumberedPath(directory, image ? "render" : "video", image ? "png" : "mp4");
    }

    std::string AppStateService::GetLastExportDirectory() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_LastExportDirectory;
    }

    void AppStateService::NotifyExported(const std::string& outputPath)
    {
        std::scoped_lock lock(m_Mutex);
        m_LastExportDirectory = std::filesystem::path(outputPath).parent_path().string();
    }

    bool AppStateService::GetTutorialCompleted() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_TutorialCompleted;
    }

    void AppStateService::SetTutorialCompleted(bool completed)
    {
        std::scoped_lock lock(m_Mutex);
        m_TutorialCompleted = completed;
    }

    void AppStateService::AddMeshScanDirectory(const std::string& directory)
    {
        if (directory.empty()) return;
        std::scoped_lock lock(m_Mutex);
        if (std::ranges::find(m_MeshScanDirectories, directory) == m_MeshScanDirectories.end())
        {
            m_MeshScanDirectories.push_back(directory);
        }
    }

    std::vector<std::string> AppStateService::GetMeshScanDirectories() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_MeshScanDirectories;
    }
}
