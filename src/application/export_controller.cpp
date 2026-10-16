module MoleHole;

import :Application.Export;
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
        std::string FormatDuration(const double seconds)
        {
            const auto total = static_cast<long long>(seconds + 0.5);
            if (total >= 3600) return std::format("{}h {:02}m {:02}s", total / 3600, total / 60 % 60, total % 60);
            if (total >= 60) return std::format("{}m {:02}s", total / 60, total % 60);
            return std::format("{}s", total);
        }

        std::string UtcTimestamp()
        {
            return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
        }
    }

    ExportController::ExportController(std::shared_ptr<Renderer> renderer, std::shared_ptr<UiState> uiState,
                                       std::shared_ptr<AppStateService> appState,
                                       std::shared_ptr<RenderSettingsService> renderSettings,
                                       std::shared_ptr<Logger> logger, const std::uint32_t bufferTargetId,
                                       RunnerProvider runner)
        : m_Renderer(std::move(renderer)), m_UiState(std::move(uiState)), m_AppState(std::move(appState)),
          m_RenderSettings(std::move(renderSettings)), m_Logger(std::move(logger)), m_BufferTargetId(bufferTargetId),
          m_Runner(std::move(runner))
    {
    }

    void ExportController::Update()
    {
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

        m_Renderer->ResizeBufferTarget(m_BufferTargetId, m_UiState->ExportResolution);

        if (m_AwaitPlay)
        {
            const auto runner = m_Runner();
            if (!runner || !runner->IsPaused())
            {
                m_AwaitPlay = false;
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
        else if (m_PipelinesReady.load(std::memory_order_acquire))
        {
            m_PendingCapture = true;
        }
        else
        {
            m_UiState->ExportStatus = "Compiling shaders...";
        }
    }

    void ExportController::Start(const ExportRequest& request)
    {
        m_Request = request;
        m_UiState->ExportResolution = {request.Width, request.Height};
        m_UiState->ExportActive = true;
        m_UiState->ExportProgress = 0.0f;
        m_UiState->ExportStatus = "Starting export...";
        m_FrameIndex = 0;
        m_TotalFrames = request.RequestKind == ExportRequest::Kind::Video
                            ? std::max(1, static_cast<int>(request.DurationSeconds * request.Framerate))
                            : 1;
        m_AllCaptured = false;
        m_EncodeStarted = false;
        m_StartTime = std::chrono::steady_clock::now();
        m_LastLog = m_StartTime;
        m_Job = std::make_shared<Job>();

        if (request.Profile)
        {
            if (m_RenderSettings->Profiles().Find(*request.Profile)) m_RenderSettings->SetProfileOverride(*request.Profile);
            else m_Logger->Warn("Export: unknown render profile '{}', using the Export context's profile", *request.Profile);
        }
        m_RenderSettings->SetExportOverrides(request.Overrides);

        const auto stats = m_Renderer->GetBufferTargetStats(m_BufferTargetId);
        m_MinSerial = (stats ? stats->FramesRendered : 0) + 3;

        m_Stepping = false;
        m_StartedPlay = false;
        m_TickDebt = 0.0;
        if (const auto runner = m_Runner(); request.RequestKind == ExportRequest::Kind::Video && runner)
        {
            runner->SetManualStepping(true);
            if (runner->IsPaused())
            {
                m_StartedPlay = true;
                m_UiState->PendingSnapshotForPlay = true;
            }
            m_AwaitPlay = true;
        }

        if (request.RequestKind == ExportRequest::Kind::Video)
        {
            m_TempDir = std::filesystem::temp_directory_path() /
                        std::format("molehole_export_{}", std::chrono::steady_clock::now().time_since_epoch().count());
            std::filesystem::create_directories(m_TempDir);
        }

        m_Logger->Info("Export started: {}x{} -> {}", request.Width, request.Height, request.OutputPath);
    }

    void ExportController::LogProgress()
    {
        if (!m_UiState->ExitWhenExportDone || m_Request.RequestKind != ExportRequest::Kind::Video) return;
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
        if (m_Stepping)
        {
            if (const auto runner = m_Runner())
            {
                m_TickDebt += runner->GetTickRate() / std::max(1, m_Request.Framerate);
                const auto ticks = static_cast<std::uint64_t>(m_TickDebt);
                m_TickDebt -= static_cast<double>(ticks);
                runner->StepAndWait(ticks);
                ++m_UiState->ExportStepSerial;
                m_MinSerial = readback.Serial + 3; // frames already in flight still show the old snapshot
            }
        }

        // The profile that rendered this frame; recorded once for the sidecar
        if (m_FrameIndex == 0) m_ProfileName = m_RenderSettings->ProfileFor(RenderContext::Export).Name;

        const auto path = m_Request.RequestKind == ExportRequest::Kind::Image
                              ? std::filesystem::path(m_Request.OutputPath)
                              : m_TempDir / std::format("frame_{:06d}.png", m_FrameIndex);
        ++m_FrameIndex;

        m_Job->WritesInFlight.fetch_add(1);
        auto write = [job = m_Job, logger = m_Logger, path, readback = std::move(readback)]() mutable
        {
            const bool bgr = readback.Format == vk::Format::eB8G8R8A8Unorm ||
                             readback.Format == vk::Format::eB8G8R8A8Srgb;
            const auto pixelCount = static_cast<std::size_t>(readback.Extent.width) * readback.Extent.height;
            std::vector<unsigned char> rgb(pixelCount * 3);
            for (std::size_t i = 0; i < pixelCount; ++i)
            {
                const auto* p = &readback.Pixels[i * 4];
                rgb[i * 3 + 0] = bgr ? p[2] : p[0];
                rgb[i * 3 + 1] = p[1];
                rgb[i * 3 + 2] = bgr ? p[0] : p[2];
            }
            if (!stbi_write_png(path.string().c_str(), static_cast<int>(readback.Extent.width),
                                static_cast<int>(readback.Extent.height), 3, rgb.data(),
                                static_cast<int>(readback.Extent.width) * 3))
            {
                logger->Error("Export: failed to write '{}'", path.string());
                job->WriteFailed = true;
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
        m_UiState->ExportStatus = m_Request.RequestKind == ExportRequest::Kind::Video
                                      ? std::format("Rendering frame {}/{}", m_FrameIndex, m_TotalFrames)
                                      : "Rendering...";

        LogProgress();

        if (m_FrameIndex >= m_TotalFrames) m_AllCaptured = true;
        else m_PendingCapture = true;
    }

    void ExportController::PollCompletion()
    {
        const int written = m_Job->FramesWritten.load();
        m_UiState->ExportProgress = static_cast<float>(written) / static_cast<float>(m_TotalFrames);

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
                               frames = (m_TempDir / "frame_%06d.png").string(), output = m_Request.OutputPath,
                               tempDir = m_TempDir]
                {
                    const auto cmd = std::format(
                        "ffmpeg -y -framerate {} -i {} -c:v libx264 -pix_fmt yuv420p {} > /dev/null 2>&1",
                        framerate, frames, output);
                    if (std::system(cmd.c_str()) != 0)
                    {
                        logger->Error("Export: ffmpeg encoding failed (is ffmpeg installed?)");
                        job->EncodeFailed = true;
                    }
                    std::error_code ec;
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
            if (!m_Job->EncodeFinished.load()) return;
        }

        Finish();
    }

    void ExportController::WriteSidecar()
    {
        RenderSidecarInfo info;
        info.OutputPath = m_Request.OutputPath;
        info.Video = m_Request.RequestKind == ExportRequest::Kind::Video;
        info.Width = m_Request.Width;
        info.Height = m_Request.Height;
        info.Frames = m_TotalFrames;
        info.Framerate = m_Request.Framerate;
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
        info.GitCommit = ReadGitCommit(std::filesystem::current_path());
        info.Gpu = m_RenderSettings->Capabilities();
        info.WallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_StartTime).count();
        info.Timestamp = UtcTimestamp();
        if (!WriteRenderSidecar(m_Request.OutputPath, info))
            m_Logger->Warn("Export: could not write {}", SidecarPathFor(m_Request.OutputPath).string());
    }

    void ExportController::Finish()
    {
        const bool failed = m_Job->EncodeFailed.load() || m_Job->WriteFailed.load();
        if (!failed) WriteSidecar();
        m_RenderSettings->SetExportOverrides({});
        m_RenderSettings->SetProfileOverride({});

        if (m_Stepping)
        {
            m_Stepping = false;
            if (const auto runner = m_Runner()) runner->SetManualStepping(false);
            m_UiState->ExportStepDelta = 0.0f;
            if (m_StartedPlay) m_UiState->PendingStopSimulation = true;
        }
        m_UiState->ExportActive = false;
        m_UiState->ExportProgress = 1.0f;
        if (failed)
        {
            m_UiState->ExportStatus = m_Job->EncodeFailed.load() ? "Failed: ffmpeg encoding error"
                                                                 : "Failed: could not write image";
        }
        else
        {
            m_UiState->ExportStatus = "Complete";
            m_Logger->Info("Export complete: {}", m_Request.OutputPath);
            m_AppState->NotifyExported(m_Request.OutputPath);
        }

        if (m_UiState->ExitWhenExportDone) Application::Instance().Stop();
    }
}
