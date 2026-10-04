export module MoleHole:Application.State;

import std;
import GPP;
import :UI.State;

export namespace MoleHole
{
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
    };
}
