export module MoleHole:Application.State;

import std;
import GPP;
import :UI.State;
import :Application.SceneCameras;
import :Rendering.SettingsService;

export namespace MoleHole
{
    [[nodiscard]] std::filesystem::path NextNumberedPath(const std::filesystem::path& directory, std::string_view stem,
                                                         std::string_view extension);

    class AppStateService final : public GPP::IHostedService
    {
    public:
        using Dependencies = std::tuple<UiState, RenderSettingsService, GPP::UiPreferences, GPP::IFileSystem, GPP::AppPaths, GPP::Logger>;

        AppStateService(std::shared_ptr<UiState> uiState, std::shared_ptr<RenderSettingsService> renderSettings,
                         std::shared_ptr<GPP::UiPreferences> preferences,
                         std::shared_ptr<GPP::IFileSystem> fileSystem, std::shared_ptr<GPP::AppPaths> appPaths,
                         std::shared_ptr<GPP::Logger> logger);

        GPP::Task<void> StartAsync(std::stop_token stopToken) override;
        GPP::Task<void> StopAsync() override;

        void NotifySceneOpened(const std::string& path);
        [[nodiscard]] std::string GetLastScenePath() const;
        [[nodiscard]] std::vector<std::string> GetRecentScenes() const;
        [[nodiscard]] std::vector<GPP::RecentEntry> GetRecents() const;
        void RemoveRecent(const std::string& path);
        [[nodiscard]] bool GetShowLauncher() const;
        void SetShowLauncher(bool show);

        void NotifyExported(const std::string& outputPath);
        [[nodiscard]] std::string GetLastExportDirectory() const;
        [[nodiscard]] std::string GetDefaultExportDirectory() const;
        void SetDefaultExportDirectory(std::string directory);
        [[nodiscard]] std::filesystem::path NextExportPath(ExportRequest::Kind kind) const;

        [[nodiscard]] bool GetTutorialCompleted() const;
        void SetTutorialCompleted(bool completed);

        void AddMeshScanDirectory(const std::string& directory);
        [[nodiscard]] std::vector<std::string> GetMeshScanDirectories() const;

        [[nodiscard]] std::optional<SceneCameraPose> LookupSceneCamera(const std::string& scenePath) const;
        void StoreSceneCamera(const std::string& scenePath, const SceneCameraPose& pose);
        void CarrySceneCamera(const std::string& fromPath, const std::string& toPath);
        void FlushSceneCameras();

        void PushRecentAsset(const std::string& path);
        [[nodiscard]] std::vector<std::string> GetRecentAssets() const;

    private:
        void Load();
        void LoadSceneCameras();
        void Save() const;

        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<RenderSettingsService> m_RenderSettings;
        std::shared_ptr<GPP::UiPreferences> m_Preferences;
        std::shared_ptr<GPP::IFileSystem> m_FileSystem;
        std::shared_ptr<GPP::AppPaths> m_AppPaths;
        std::shared_ptr<GPP::Logger> m_Logger;

        std::filesystem::path m_StatePath;
        bool m_Persist = true;
        SceneCameraCache m_SceneCameras;

        mutable std::mutex m_Mutex;
        std::string m_LastScenePath;
        GPP::RecentList m_Recents;
        std::vector<std::string> m_RemovedRecents;
        bool m_ShowLauncher = true;
        std::string m_LastExportDirectory;
        std::string m_DefaultExportDirectory;
        std::vector<std::string> m_MeshScanDirectories;
        std::vector<std::string> m_RecentAssets;
        bool m_TutorialCompleted = false;
    };
}
