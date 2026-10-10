export module MoleHole:Application.State;

import std;
import GPP;
import :UI.State;

export namespace MoleHole
{
    [[nodiscard]] std::filesystem::path NextNumberedPath(const std::filesystem::path& directory, std::string_view stem,
                                                         std::string_view extension);

    class AppStateService final : public GPP::IHostedService
    {
    public:
        using Dependencies = std::tuple<UiState, GPP::UiPreferences, GPP::IFileSystem, GPP::Logger>;

        AppStateService(std::shared_ptr<UiState> uiState, std::shared_ptr<GPP::UiPreferences> preferences,
                         std::shared_ptr<GPP::IFileSystem> fileSystem, std::shared_ptr<GPP::Logger> logger);

        GPP::Task<void> StartAsync(std::stop_token stopToken) override;
        GPP::Task<void> StopAsync() override;

        void NotifySceneOpened(const std::string& path);
        [[nodiscard]] std::string GetLastScenePath() const;
        [[nodiscard]] std::vector<std::string> GetRecentScenes() const;

        void NotifyExported(const std::string& outputPath);
        [[nodiscard]] std::string GetLastExportDirectory() const;
        [[nodiscard]] std::string GetDefaultExportDirectory() const;
        void SetDefaultExportDirectory(std::string directory);
        [[nodiscard]] std::filesystem::path NextExportPath(ExportRequest::Kind kind) const;

        [[nodiscard]] bool GetTutorialCompleted() const;
        void SetTutorialCompleted(bool completed);

        void AddMeshScanDirectory(const std::string& directory);
        [[nodiscard]] std::vector<std::string> GetMeshScanDirectories() const;

    private:
        void Load();
        void Save() const;

        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<GPP::UiPreferences> m_Preferences;
        std::shared_ptr<GPP::IFileSystem> m_FileSystem;
        std::shared_ptr<GPP::Logger> m_Logger;

        std::filesystem::path m_StatePath;
        bool m_Persist = true;

        mutable std::mutex m_Mutex;
        std::string m_LastScenePath;
        std::vector<std::string> m_RecentScenes;
        std::string m_LastExportDirectory;
        std::string m_DefaultExportDirectory{".gpp/exports"};
        std::vector<std::string> m_MeshScanDirectories;
        bool m_TutorialCompleted = false;
    };
}
