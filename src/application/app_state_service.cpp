module;
#include <yaml-cpp/yaml.h>
module MoleHole;

import :Application.State;
import std;
import glm;
import GPP;

using namespace GPP;

namespace MoleHole
{
    AppStateService::AppStateService(std::shared_ptr<UiState> uiState, std::shared_ptr<UiPreferences> preferences,
                                      std::shared_ptr<IFileSystem> fileSystem, std::shared_ptr<Logger> logger)
        : m_UiState(std::move(uiState)), m_Preferences(std::move(preferences)),
          m_FileSystem(std::move(fileSystem)), m_Logger(std::move(logger))
    {
    }

    Task<void> AppStateService::StartAsync(std::stop_token)
    {
        m_StatePath = m_FileSystem->ResolvePath("molehole_state.yaml");
        m_Persist = !m_UiState->ExitWhenExportDone;
        if (m_Persist)
        {
            Load();
        }
        co_return;
    }

    Task<void> AppStateService::StopAsync()
    {
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
            if (const auto recent = node["RecentScenes"]; recent && recent.IsSequence())
            {
                for (const auto& entry : recent)
                {
                    m_RecentScenes.push_back(entry.as<std::string>());
                }
            }
            if (const auto camera = node["Camera"])
            {
                if (const auto v = camera["Position"]) m_UiState->CameraPosition = v.as<glm::vec3>();
                if (const auto v = camera["Yaw"]) m_UiState->CameraYaw = v.as<float>();
                if (const auto v = camera["Pitch"]) m_UiState->CameraPitch = v.as<float>();
                if (const auto v = camera["Speed"]) m_UiState->CameraSpeed = v.as<float>();
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
            }
            if (const auto render = node["Render"])
            {
                if (const auto v = render["DebugMode"]) m_UiState->Render.DebugMode = v.as<int>();
                if (const auto v = render["PhysicallyAccurate"]) m_UiState->Render.PhysicallyAccurate = v.as<bool>();
            }
            if (const auto v = node["LastExportDirectory"]; v && v.IsScalar())
            {
                m_LastExportDirectory = v.as<std::string>();
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

            YAML::Node recentNode;
            {
                std::scoped_lock lock(m_Mutex);
                for (const auto& scene : m_RecentScenes)
                {
                    recentNode.push_back(scene);
                }
            }
            root["RecentScenes"] = recentNode;

            root["Camera"]["Position"] = m_UiState->CameraPosition;
            root["Camera"]["Yaw"] = m_UiState->CameraYaw;
            root["Camera"]["Pitch"] = m_UiState->CameraPitch;
            root["Camera"]["Speed"] = m_UiState->CameraSpeed;
            root["Camera"]["MouseSensitivity"] = m_UiState->CameraMouseSensitivity;

            root["UI"]["Font"] = m_Preferences->GetFontName();
            root["UI"]["FontSize"] = m_Preferences->GetFontSize();
            root["UI"]["UiScale"] = m_Preferences->GetUiScale();
            root["UI"]["ShowViewportHud"] = m_UiState->ShowViewportHud;

            root["Render"]["DebugMode"] = m_UiState->Render.DebugMode;
            root["Render"]["PhysicallyAccurate"] = m_UiState->Render.PhysicallyAccurate;

            root["LastExportDirectory"] = GetLastExportDirectory();

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

            std::ofstream file(m_StatePath, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                m_Logger->Error("AppStateService: failed to open '{}' for writing", m_StatePath.string());
                return;
            }
            file << root;
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
        std::scoped_lock lock(m_Mutex);
        return m_RecentScenes;
    }

    void AppStateService::NotifySceneOpened(const std::string& path)
    {
        if (path.empty())
        {
            return;
        }
        std::scoped_lock lock(m_Mutex);
        m_LastScenePath = path;
        std::erase(m_RecentScenes, path);
        m_RecentScenes.insert(m_RecentScenes.begin(), path);
        constexpr std::size_t kMaxRecent = 8;
        if (m_RecentScenes.size() > kMaxRecent)
        {
            m_RecentScenes.resize(kMaxRecent);
        }
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
