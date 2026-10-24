module;
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

module MoleHole;

import :Application.Export;
import :Application.ExportSupport;
import :Rendering.Sidecar;
import std;
import glm;
import vulkan;
import GPP;

using namespace GPP;

namespace MoleHole
{
    namespace
    {
        bool Animated(const ExportRequest& request)
        {
            return request.RequestKind != ExportRequest::Kind::Image;
        }

        std::string KindLabel(const ExportRequest::Kind kind)
        {
            switch (kind)
            {
            case ExportRequest::Kind::Video: return "video";
            case ExportRequest::Kind::Sequence: return "image sequence";
            default: return "image";
            }
        }

        std::filesystem::path PartialPath(const std::filesystem::path& output)
        {
            return output.parent_path() / (output.stem().string() + ".partial" + output.extension().string());
        }

        int RunEncoder(std::atomic<int>& encoderPid, const std::atomic<bool>& cancelled, const std::vector<std::string>& args)
        {
            std::vector<char*> argv;
            for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
            argv.push_back(nullptr);

            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
            posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
            pid_t pid{};
            const int rc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
            posix_spawn_file_actions_destroy(&actions);
            if (rc != 0) return -1;
            encoderPid = pid;
            if (cancelled.load()) kill(pid, SIGTERM);
            int status = 0;
            const bool waited = waitpid(pid, &status, 0) >= 0;
            encoderPid = 0;
            return waited && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }

        std::string UtcTimestamp()
        {
            return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
        }
    }

    ExportController::ExportController(std::shared_ptr<Renderer> renderer, std::shared_ptr<UiState> uiState,
                                       std::shared_ptr<AppStateService> appState,
                                       std::shared_ptr<RenderSettingsService> renderSettings,
                                       std::shared_ptr<AppPaths> appPaths, std::shared_ptr<Logger> logger,
                                       const std::uint32_t bufferTargetId, RunnerProvider runner)
        : m_Renderer(std::move(renderer)), m_UiState(std::move(uiState)), m_AppState(std::move(appState)),
          m_RenderSettings(std::move(renderSettings)), m_AppPaths(std::move(appPaths)), m_Logger(std::move(logger)),
          m_BufferTargetId(bufferTargetId), m_Runner(std::move(runner))
    {
    }

    void ExportController::SetPipelineFailure(std::optional<std::string> failure)
    {
        std::scoped_lock lock(m_FailureMutex);
        m_PipelineFailure = std::move(failure);
    }

    void ExportController::CheckWatchdog()
    {
        if (!m_UiState->ExitWhenExportDone) return;
        const auto now = std::chrono::steady_clock::now();
        if (!m_LastProgress) m_LastProgress = now;
        const float timeout = m_UiState->ExportTimeoutSeconds;
        if (timeout <= 0.0f) return;
        const double idle = std::chrono::duration<double>(now - *m_LastProgress).count();
        if (idle > timeout)
            Abort(4, std::format("no progress for {:.1f} s ({}), giving up (see --export-timeout)", idle,
                                 m_UiState->ExportActive ? m_UiState->ExportStatus : "waiting for the scene"));
    }

    void ExportController::SyncTask()
    {
        if (!m_Task) return;
        m_Task->SetFraction(m_UiState->ExportProgress);
        m_Task->SetDetail(m_UiState->ExportStatus);
    }

    void ExportController::Abort(const int exitCode, const std::string& reason)
    {
        m_Logger->Error("Export failed: {}", reason);
        if (m_Task) std::exchange(m_Task, nullptr)->Finish(false);
        m_Logger->Notifications()->Post(GPP::NotificationKind::Error, "Export failed", reason, 0.0f, "export");
        m_UiState->ExportExitCode = exitCode;
        m_UiState->ExportStatus = "Failed: " + reason;
        m_UiState->ExportActive = false;
        m_UiState->ExportRemainingSeconds = -1.0;
        Teardown();
        if (m_UiState->ExitWhenExportDone) Application::Instance().Stop();
    }

    void ExportController::Cancel()
    {
        const bool sequence = m_Request.RequestKind == ExportRequest::Kind::Sequence;
        m_Logger->Info("Export cancelled after {} of {} frames{}", m_FrameIndex, m_TotalFrames,
                       sequence ? std::format(", {} frames kept in {}", m_Job->FramesWritten.load(), m_Request.OutputPath) : "");
        if (m_Task) std::exchange(m_Task, nullptr)->Finish(false);
        m_Logger->Notifications()->Post(GPP::NotificationKind::Warning, "Export cancelled", "", 4.0f, "export");
        m_UiState->ExportStatus = "Cancelled";
        m_UiState->ExportProgress = 0.0f;
        m_UiState->ExportRemainingSeconds = -1.0;
        m_UiState->ExportActive = false;
        Teardown();
        if (m_UiState->ExitWhenExportDone)
        {
            m_UiState->ExportExitCode = 6;
            Application::Instance().Stop();
        }
    }

    void ExportController::Teardown()
    {
        m_Job->Cancelled = true;
        if (const int pid = m_Job->EncoderPid.load(); pid > 0) kill(pid, SIGTERM);
        m_RenderSettings->SetExportOverrides({});
        m_RenderSettings->SetProfileOverride({});
        if (m_ManualStepping)
        {
            if (const auto runner = m_Runner()) runner->SetManualStepping(false);
            m_ManualStepping = false;
        }
        m_UiState->ExportStepDelta = 0.0f;
        if (m_StartedPlay)
        {
            if (!m_UiState->PendingSnapshotForPlay.exchange(false)) m_UiState->PendingStopSimulation = true;
            m_StartedPlay = false;
        }
        m_Stepping = false;
        m_AwaitPlay = false;
        m_PendingCapture = false;
        DiscardTempDir();
    }

    void ExportController::DiscardTempDir()
    {
        auto dir = std::exchange(m_TempDir, {});
        if (dir.empty() || m_EncodeStarted) return;
        auto remove = [job = m_Job, dir]
        {
            while (job->WritesInFlight.load() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        };
        try
        {
            ThreadPool::Instance().Submit(std::move(remove));
        }
        catch (const std::exception&)
        {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    }

    void ExportController::UpdateTiming()
    {
        const auto now = std::chrono::steady_clock::now();
        m_UiState->ExportElapsedSeconds = std::chrono::duration<double>(now - m_StartTime).count();
        m_UiState->ExportRemainingSeconds = -1.0;
        if (m_AllCaptured || m_FrameIndex < 2) return;
        const double sinceFirst = std::chrono::duration<double>(m_LastCaptureTime - m_FirstFrameTime).count();
        if (const auto remaining = EstimateRemainingSeconds(sinceFirst, m_FrameIndex, m_TotalFrames))
        {
            const double sinceLast = std::chrono::duration<double>(now - m_LastCaptureTime).count();
            m_UiState->ExportRemainingSeconds = std::max(0.0, *remaining - sinceLast);
        }
    }

    std::filesystem::path ExportController::SidecarOutput() const
    {
        if (m_Request.RequestKind != ExportRequest::Kind::Sequence) return m_Request.OutputPath;
        return std::filesystem::path(m_Request.OutputPath) / (SanitizeSequenceName(m_Request.SequenceName) + ".png");
    }

    void ExportController::Update()
    {
        if (m_UiState->ExitWhenExportDone && m_UiState->ExportExitCode != 0) return;
        if (m_UiState->ExportCancelRequested.exchange(false))
        {
            if (m_UiState->ExportActive) Cancel();
            else m_UiState->PendingExport.Take();
        }
        if (m_UiState->ExitWhenExportDone && m_UiState->ExportExitCode != 0) return;
        CheckWatchdog();
        if (m_UiState->ExitWhenExportDone && m_UiState->ExportExitCode != 0) return;

        if (m_PendingCapture)
        {
            m_PendingCapture = false;
            CaptureFrame();
        }

        if (!m_UiState->ExportActive)
        {
            const bool sceneReady = !m_UiState->ExitWhenExportDone || m_Runner();
            if (sceneReady)
            {
                if (auto request = m_UiState->PendingExport.Take()) Start(*request);
            }
        }

        if (!m_UiState->ExportActive) return;

        UpdateTiming();
        m_Renderer->ResizeBufferTarget(m_BufferTargetId, m_UiState->ExportResolution);

        if (m_AwaitPlay)
        {
            const auto runner = m_Runner();
            if (!runner || !runner->IsPaused())
            {
                m_AwaitPlay = false;
                m_LastProgress = std::chrono::steady_clock::now();
                m_Stepping = runner != nullptr;
                m_UiState->ExportStepDelta = 1.0f / static_cast<float>(std::max(1, m_Request.Framerate));
            }
            else
            {
                m_UiState->ExportStatus = "Starting simulation...";
                return;
            }
        }

        if (m_AllCaptured)
        {
            PollCompletion();
        }
        else if (m_PipelinesReady.load(std::memory_order_acquire) && FramesSettled())
        {
            if (!m_ReadySeen)
            {
                m_ReadySeen = true;
                m_LastProgress = std::chrono::steady_clock::now();
            }
            m_PendingCapture = true;
        }
        else
        {
            std::optional<std::string> failure;
            {
                std::scoped_lock lock(m_FailureMutex);
                failure = m_PipelineFailure;
            }
            if (failure)
            {
                if (m_UiState->ExitWhenExportDone) Abort(3, *failure);
                else m_UiState->ExportStatus = "Shader compile failed: " + *failure;
            }
            else if (!m_ReadySeen) m_UiState->ExportStatus = "Compiling shaders...";
        }
    }

    // Pipelines are created for the settings a frame is built with, so readiness only counts once frames built
    // with the export's own settings are being rendered
    bool ExportController::FramesSettled() const
    {
        const auto stats = m_Renderer->GetBufferTargetStats(m_BufferTargetId);
        return stats && stats->FramesRendered >= m_MinSerial;
    }

    void ExportController::Start(const ExportRequest& request)
    {
        m_Request = request;
        if (request.RequestKind == ExportRequest::Kind::Sequence)
        {
            std::error_code ec;
            std::filesystem::create_directories(request.OutputPath, ec);
            if (ec)
            {
                Abort(5, std::format("cannot create '{}': {}", request.OutputPath, ec.message()));
                return;
            }
        }
        m_UiState->ExportResolution = {request.Width, request.Height};
        m_UiState->ExportActive = true;
        m_UiState->ExportProgress = 0.0f;
        m_UiState->ExportStatus = "Starting export...";
        m_Task = m_Logger->Notifications()->BeginTask(std::format("Exporting {}", KindLabel(request.RequestKind)));
        m_Task->SetFraction(0.0f);
        m_Task->SetDetail(std::filesystem::path(request.OutputPath).filename().string());
        m_FrameIndex = 0;
        m_TotalFrames = Animated(request)
                            ? std::max(1, static_cast<int>(request.DurationSeconds * request.Framerate))
                            : 1;
        m_AllCaptured = false;
        m_EncodeStarted = false;
        m_ReadySeen = false;
        m_LastWritten = -1;
        m_StartTime = std::chrono::steady_clock::now();
        m_FirstFrameTime = m_StartTime;
        m_LastCaptureTime = m_StartTime;
        m_UiState->ExportElapsedSeconds = 0.0;
        m_UiState->ExportRemainingSeconds = -1.0;
        m_LastProgress = m_StartTime;
        m_LastLog = m_StartTime;
        m_Job = std::make_shared<Job>();

        if (request.Profile)
        {
            if (m_RenderSettings->Profiles().Find(*request.Profile)) m_RenderSettings->SetProfileOverride(*request.Profile);
            else m_Logger->Warn("Export: unknown render profile '{}', using the Export context's profile", *request.Profile);
        }
        m_RenderSettings->SetExportOverrides(request.Overrides);

        m_SimStart = 0.0;
        m_TimeScale = 1.0;
        if (const auto runner = m_Runner())
        {
            if (request.TimeScale)
            {
                runner->EnqueueEdit([scale = *request.TimeScale](Scene& scene) { scene.SetTimeScale(scale); });
            }
            m_TimeScale = request.TimeScale.value_or(runner->Metadata().TimeScale);
            if (request.SimStart) runner->SetSimTime(*request.SimStart);
            m_SimStart = runner->AcquireSnapshot()->Clock().Time;
        }

        const auto stats = m_Renderer->GetBufferTargetStats(m_BufferTargetId);
        m_MinSerial = (stats ? stats->FramesRendered : 0) + 3;

        m_Stepping = false;
        m_StartedPlay = false;
        m_TickDebt = 0.0;
        if (const auto runner = m_Runner(); Animated(request) && runner)
        {
            runner->SetManualStepping(true);
            m_ManualStepping = true;
            if (runner->IsPaused())
            {
                m_StartedPlay = true;
                m_UiState->PendingSnapshotForPlay = true;
            }
            m_AwaitPlay = true;
        }

        if (request.RequestKind == ExportRequest::Kind::Video)
        {
            const auto tempRoot = m_AppPaths->IsInstalled() ? m_AppPaths->TempDir() : std::filesystem::temp_directory_path();
            m_TempDir = tempRoot /
                        std::format("molehole_export_{}_{}", GPP::CurrentProcessId(), std::chrono::steady_clock::now().time_since_epoch().count());
            std::filesystem::create_directories(m_TempDir);
        }

        m_Logger->Info("Export started: {}x{} -> {}", request.Width, request.Height, request.OutputPath);
    }

    void ExportController::LogProgress()
    {
        if (!m_UiState->ExitWhenExportDone || !Animated(m_Request)) return;
        const auto now = std::chrono::steady_clock::now();
        const bool last = m_FrameIndex >= m_TotalFrames;
        if (!last && now - m_LastLog < std::chrono::seconds(1)) return;
        m_LastLog = now;
        const double elapsed = std::chrono::duration<double>(now - m_StartTime).count();
        const double remaining = elapsed / m_FrameIndex * (m_TotalFrames - m_FrameIndex);
        m_Logger->Info("Export {}/{} frames ({:.1f}%) | elapsed {} | remaining {}", m_FrameIndex, m_TotalFrames,
                       100.0 * m_FrameIndex / m_TotalFrames, FormatDuration(elapsed), FormatDuration(remaining));
    }

    void ExportController::CaptureFrame()
    {
        if (m_Job->WritesInFlight.load() >= 2)
        {
            m_PendingCapture = true;
            return;
        }

        auto readback = m_Renderer->ReadBackBufferTarget(m_BufferTargetId, m_MinSerial, m_UiState->ExportResolution);
        if (readback.Pixels.empty())
        {
            m_PendingCapture = true;
            return;
        }
        m_MinSerial = readback.Serial + 1;
        m_LastProgress = std::chrono::steady_clock::now();
        if (const auto stats = m_Renderer->GetBufferTargetStats(m_BufferTargetId))
        {
            m_LastGpuMs = stats->GpuMs;
            m_LastPassMs.clear();
            for (const auto& pass : stats->Passes) m_LastPassMs.emplace_back(pass.Name, pass.Ms);
        }
        if (m_Stepping)
        {
            if (const auto runner = m_Runner())
            {
                m_TickDebt += runner->GetTickRate() / std::max(1, m_Request.Framerate);
                const auto ticks = static_cast<std::uint64_t>(m_TickDebt);
                m_TickDebt -= static_cast<double>(ticks);
                runner->StepAndWait(ticks);
                runner->SetSimTime(m_SimStart + static_cast<double>(m_FrameIndex + 1) * m_TimeScale / std::max(1, m_Request.Framerate));
                ++m_UiState->ExportStepSerial;
                m_MinSerial = readback.Serial + 3;
            }
        }

        if (m_FrameIndex == 0) m_ProfileName = m_RenderSettings->ProfileFor(RenderContext::Export).Name;

        std::filesystem::path path;
        switch (m_Request.RequestKind)
        {
        case ExportRequest::Kind::Image: path = m_Request.OutputPath; break;
        case ExportRequest::Kind::Video: path = m_TempDir / std::format("frame_{:06d}.png", m_FrameIndex); break;
        case ExportRequest::Kind::Sequence:
            path = SequenceFramePath(m_Request.OutputPath, m_Request.SequenceName, m_FrameIndex + 1);
            break;
        }
        if (m_FrameIndex == 0) m_FirstFrameTime = std::chrono::steady_clock::now();
        m_LastCaptureTime = std::chrono::steady_clock::now();
        ++m_FrameIndex;

        m_Job->WritesInFlight.fetch_add(1);
        auto write = [job = m_Job, logger = m_Logger, path, readback = std::move(readback)]() mutable
        {
            const bool bgr = readback.Format == vk::Format::eB8G8R8A8Unorm ||
                             readback.Format == vk::Format::eB8G8R8A8Srgb;
            if (job->Cancelled.load())
            {
                job->WritesInFlight.fetch_sub(1);
                return;
            }
            const auto pixelCount = static_cast<std::size_t>(readback.Extent.width) * readback.Extent.height;
            std::vector<unsigned char> rgb(pixelCount * 3);
            for (std::size_t i = 0; i < pixelCount; ++i)
            {
                const auto* p = &readback.Pixels[i * 4];
                rgb[i * 3 + 0] = bgr ? p[2] : p[0];
                rgb[i * 3 + 1] = p[1];
                rgb[i * 3 + 2] = bgr ? p[0] : p[2];
            }
            auto partial = path;
            partial += ".partial";
            std::error_code ec;
            if (!stbi_write_png(partial.string().c_str(), static_cast<int>(readback.Extent.width),
                                static_cast<int>(readback.Extent.height), 3, rgb.data(),
                                static_cast<int>(readback.Extent.width) * 3))
            {
                logger->Error("Export: failed to write '{}'", path.string());
                job->WriteFailed = true;
                std::filesystem::remove(partial, ec);
            }
            else if (job->Cancelled.load())
            {
                std::filesystem::remove(partial, ec);
            }
            else
            {
                std::filesystem::rename(partial, path, ec);
                if (ec)
                {
                    logger->Error("Export: failed to write '{}': {}", path.string(), ec.message());
                    job->WriteFailed = true;
                    std::filesystem::remove(partial, ec);
                }
            }
            job->FramesWritten.fetch_add(1);
            job->WritesInFlight.fetch_sub(1);
        };
        try
        {
            ThreadPool::Instance().Submit(std::move(write));
        }
        catch (const std::exception&)
        {
            m_Job->WritesInFlight.fetch_sub(1);
            m_Job->WriteFailed = true;
        }

        m_UiState->ExportProgress = static_cast<float>(m_FrameIndex) / static_cast<float>(m_TotalFrames);
        m_UiState->ExportStatus = Animated(m_Request) ? std::format("Rendering frame {}/{}", m_FrameIndex, m_TotalFrames)
                                                      : "Rendering...";
        SyncTask();

        LogProgress();

        if (m_FrameIndex >= m_TotalFrames) m_AllCaptured = true;
        else m_PendingCapture = true;
    }

    void ExportController::PollCompletion()
    {
        const int written = m_Job->FramesWritten.load();
        if (written != m_LastWritten)
        {
            m_LastWritten = written;
            m_LastProgress = std::chrono::steady_clock::now();
        }
        m_UiState->ExportProgress = static_cast<float>(written) / static_cast<float>(m_TotalFrames);
        SyncTask();

        if (written < m_TotalFrames && m_Job->WritesInFlight.load() > 0)
        {
            m_UiState->ExportStatus = std::format("Writing frames {}/{}", written, m_TotalFrames);
            return;
        }

        if (m_Request.RequestKind == ExportRequest::Kind::Video)
        {
            if (!m_EncodeStarted)
            {
                m_EncodeStarted = true;
                m_UiState->ExportStatus = "Encoding video...";
                auto encode = [job = m_Job, logger = m_Logger, framerate = m_Request.Framerate,
                               frames = (m_TempDir / "frame_%06d.png").string(), output = std::filesystem::path(m_Request.OutputPath),
                               tempDir = m_TempDir]
                {
                    const auto partial = PartialPath(output);
                    const std::vector<std::string> args{"ffmpeg", "-y", "-framerate", std::to_string(framerate), "-i", frames,
                                                        "-c:v", "libx264", "-pix_fmt", "yuv420p", partial.string()};
                    const int code = job->Cancelled.load() ? -1 : RunEncoder(job->EncoderPid, job->Cancelled, args);
                    std::error_code ec;
                    if (code == 0 && !job->Cancelled.load())
                    {
                        std::filesystem::rename(partial, output, ec);
                        if (ec)
                        {
                            logger->Error("Export: could not move the video to '{}': {}", output.string(), ec.message());
                            job->EncodeFailed = true;
                        }
                    }
                    else
                    {
                        if (!job->Cancelled.load())
                        {
                            logger->Error("Export: ffmpeg encoding failed (is ffmpeg installed?)");
                            job->EncodeFailed = true;
                        }
                        std::filesystem::remove(partial, ec);
                    }
                    std::filesystem::remove_all(tempDir, ec);
                    job->EncodeFinished = true;
                };
                try
                {
                    ThreadPool::Instance().Submit(std::move(encode));
                }
                catch (const std::exception&)
                {
                    m_Job->EncodeFailed = true;
                    m_Job->EncodeFinished = true;
                }
                return;
            }
            if (!m_Job->EncodeFinished.load())
            {
                m_LastProgress = std::chrono::steady_clock::now();
                return;
            }
        }

        Finish();
    }

    void ExportController::WriteSidecar()
    {
        RenderSidecarInfo info;
        info.OutputPath = SidecarOutput().string();
        info.Video = m_Request.RequestKind == ExportRequest::Kind::Video;
        info.Width = m_Request.Width;
        info.Height = m_Request.Height;
        info.Frames = m_TotalFrames;
        info.Framerate = m_Request.Framerate;
        info.SimStart = m_SimStart;
        info.TimeScale = m_TimeScale;
        info.ScenePath = m_UiState->CurrentScenePath;
        info.SceneName = m_UiState->CurrentSceneName;
        if (const auto runner = m_Runner())
        {
            if (const auto snapshot = runner->AcquireSnapshot()) info.SceneYaml = snapshot->SerializeToYaml();
        }
        info.ProfileName = m_ProfileName;
        info.Settings = m_RenderSettings->Effective();
        info.Overrides = m_Request.Overrides;
        info.CameraPositionM = m_UiState->ViewPositionM;
        info.CameraYaw = m_UiState->CameraYaw;
        info.CameraPitch = m_UiState->CameraPitch;
        info.CameraFov = m_UiState->ViewFov;
        if (m_AppPaths->IsInstalled())
        {
#ifdef MOLEHOLE_GIT_COMMIT
            info.GitCommit = MOLEHOLE_GIT_COMMIT;
#else
            info.GitCommit = "unknown";
#endif
        }
        else
        {
            info.GitCommit = ReadGitCommit(std::filesystem::current_path());
        }
        info.Gpu = m_RenderSettings->Capabilities();
        info.GpuMs = m_LastGpuMs;
        info.GpuPassMs = m_LastPassMs;
        info.WallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_StartTime).count();
        info.Timestamp = UtcTimestamp();
        if (!WriteRenderSidecar(info.OutputPath, info))
            m_Logger->Warn("Export: could not write {}", SidecarPathFor(info.OutputPath).string());
    }

    void ExportController::Finish()
    {
        const bool failed = m_Job->EncodeFailed.load() || m_Job->WriteFailed.load();
        if (!failed) WriteSidecar();
        Teardown();
        m_UiState->ExportActive = false;
        m_UiState->ExportProgress = 1.0f;
        m_UiState->ExportRemainingSeconds = -1.0;
        if (m_Task) std::exchange(m_Task, nullptr)->Finish(!failed);
        if (failed)
        {
            m_UiState->ExportExitCode = 5;
            m_UiState->ExportStatus = m_Job->EncodeFailed.load() ? "Failed: ffmpeg encoding error"
                                                                 : "Failed: could not write image";
            m_Logger->Notifications()->Post(GPP::NotificationKind::Error, "Export failed",
                                            m_UiState->ExportStatus, 0.0f, "export");
        }
        else
        {
            m_UiState->ExportStatus = "Complete";
            m_Logger->Info("Export complete: {}", m_Request.OutputPath);
            m_Logger->Notifications()->Post(GPP::NotificationKind::Success,
                                            std::format("Export finished ({})", KindLabel(m_Request.RequestKind)),
                                            m_Request.OutputPath, 8.0f, "export");
            m_AppState->NotifyExported(SidecarOutput().string());
        }

        if (m_UiState->ExitWhenExportDone) Application::Instance().Stop();
    }
}
