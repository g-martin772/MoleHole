export module MoleHole:Application.Export;

import std;
import GPP;
import :UI.State;
import :Application.State;
import :Rendering.SettingsService;

export namespace MoleHole
{
    // Runs image and video exports of the viewport buffer: waits for the shaders, steps the simulation per frame,
    // reads frames back, encodes, and leaves a `.render.yaml` next to the result.
    class ExportController
    {
    public:
        using RunnerProvider = std::function<std::shared_ptr<GPP::SimulationRunner>()>;

        ExportController(std::shared_ptr<GPP::Renderer> renderer, std::shared_ptr<UiState> uiState,
                         std::shared_ptr<AppStateService> appState, std::shared_ptr<RenderSettingsService> renderSettings,
                         std::shared_ptr<GPP::Logger> logger, std::uint32_t bufferTargetId, RunnerProvider runner);

        // UI thread, once per update
        void Update();
        // Render thread: every stage's shaders are compiled
        void SetPipelinesReady(const bool ready) noexcept { m_PipelinesReady.store(ready, std::memory_order_release); }

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

        std::shared_ptr<GPP::Renderer> m_Renderer;
        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<AppStateService> m_AppState;
        std::shared_ptr<RenderSettingsService> m_RenderSettings;
        std::shared_ptr<GPP::Logger> m_Logger;
        std::uint32_t m_BufferTargetId;
        RunnerProvider m_Runner;

        std::atomic<bool> m_PipelinesReady{false};
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
        std::shared_ptr<Job> m_Job = std::make_shared<Job>();
        std::filesystem::path m_TempDir;
        std::string m_ProfileName;
    };
}
