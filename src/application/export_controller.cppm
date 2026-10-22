export module MoleHole:Application.Export;

import std;
import GPP;
import :UI.State;
import :Application.State;
import :Rendering.SettingsService;

export namespace MoleHole
{
    class ExportController
    {
    public:
        using RunnerProvider = std::function<std::shared_ptr<GPP::SimulationRunner>()>;

        ExportController(std::shared_ptr<GPP::Renderer> renderer, std::shared_ptr<UiState> uiState,
                         std::shared_ptr<AppStateService> appState, std::shared_ptr<RenderSettingsService> renderSettings,
                         std::shared_ptr<GPP::AppPaths> appPaths, std::shared_ptr<GPP::Logger> logger, std::uint32_t bufferTargetId, RunnerProvider runner);

        void Update();
        void SetPipelinesReady(const bool ready) noexcept { m_PipelinesReady.store(ready, std::memory_order_release); }
        void SetPipelineFailure(std::optional<std::string> failure);

    private:
        struct Job
        {
            std::atomic<int> WritesInFlight{0};
            std::atomic<int> FramesWritten{0};
            std::atomic<bool> WriteFailed{false};
            std::atomic<bool> EncodeFinished{false};
            std::atomic<bool> EncodeFailed{false};
        };

        void Start(const ExportRequest& request);
        void CaptureFrame();
        void PollCompletion();
        void Finish();
        void LogProgress();
        void WriteSidecar();
        void Abort(int exitCode, const std::string& reason);
        void CheckWatchdog();
        void SyncTask();

        std::shared_ptr<GPP::Renderer> m_Renderer;
        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<AppStateService> m_AppState;
        std::shared_ptr<RenderSettingsService> m_RenderSettings;
        std::shared_ptr<GPP::AppPaths> m_AppPaths;
        std::shared_ptr<GPP::Logger> m_Logger;
        std::uint32_t m_BufferTargetId;
        RunnerProvider m_Runner;

        std::atomic<bool> m_PipelinesReady{false};
        std::mutex m_FailureMutex;
        std::optional<std::string> m_PipelineFailure;
        std::optional<std::chrono::steady_clock::time_point> m_LastProgress;
        bool m_ReadySeen{false};
        int m_LastWritten{-1};
        bool m_PendingCapture{false};
        ExportRequest m_Request;
        int m_FrameIndex{0};
        int m_TotalFrames{0};
        bool m_AllCaptured{false};
        bool m_EncodeStarted{false};
        std::uint64_t m_MinSerial{0};
        bool m_Stepping{false};
        bool m_AwaitPlay{false};
        std::chrono::steady_clock::time_point m_StartTime;
        std::chrono::steady_clock::time_point m_LastLog;
        bool m_StartedPlay{false};
        double m_TickDebt{0.0};
        double m_SimStart{0.0};
        double m_TimeScale{1.0};
        std::shared_ptr<GPP::ProgressTask> m_Task;
        std::shared_ptr<Job> m_Job = std::make_shared<Job>();
        std::filesystem::path m_TempDir;
        std::string m_ProfileName;
        double m_LastGpuMs{0.0};
        std::vector<std::pair<std::string, double>> m_LastPassMs;
    };
}
