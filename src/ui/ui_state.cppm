export module MoleHole:UI.State;

import std;
import glm;
import GPP;
import imgui;
import :Rendering.Frame;

export namespace MoleHole
{
    constexpr std::uint32_t kViewportBufferId = 1;
    constexpr std::uint32_t kIntroBufferId = 2;

    struct RenderToggles
    {
        bool GravitationalLensing = true;
        bool GravitationalRedshift = true;
        bool AccretionDisk = true;
        bool AccretionDiskVolumetric = false;
        bool DopplerBeaming = true;
        bool RenderBlackHoles = true;
        bool RenderSpheres = true;
        bool PhysicallyAccurate = false;
        bool ShowGravityGrid = false;
        bool ShowPhysicsDebug = false;
        bool ShowObjectPaths = false;
        int DebugMode = 0;
        // 0=Schwarzschild, 1=Kerr, 2=Reissner-Nordstrom, 3=Kerr-Newman
        int MetricType = 1;
        float AccDiskHeight = 0.2f;
        float AccDiskSpeed = 0.5f;
        float AccDiskNoiseScale = 1.0f;
        float AccDiskNoiseLOD = 5.0f;
        float RayStepSize = 0.01f;
        int MaxRaySteps = 100000;
        float AdaptiveStepRate = 0.8f;
        bool BloomEnabled = true;
        float BloomThreshold = 0.6f;
        int BloomBlurPasses = 5;
        float BloomIntensity = 5.0f;
        bool BloomDebug = false;
        bool AntiAliasing = true;
        bool LensFlare = true;
        float LensFlareIntensity = 0.3f;
        float LensFlareThreshold = 2.0f;
        bool ThirdPerson = false;
        float ThirdPersonDistance = 10.0f;
        float ThirdPersonHeight = 3.0f;
        int FrameMode = static_cast<int>(MoleHole::FrameMode::PrimaryBlackHole);
        GPP::Units::Precision GpuPrecision = GPP::Units::Precision::F32;
    };

    enum class GizmoOperation { Translate, Rotate, Scale };
    enum class GizmoMode { Local, World };

    struct ExportRequest
    {
        enum class Kind { Image, Video } RequestKind = Kind::Image;
        std::string OutputPath;
        std::uint32_t Width = 1920;
        std::uint32_t Height = 1080;
        float DurationSeconds = 10.0f;
        int Framerate = 60;
        std::optional<float> RayStepSize;
        std::optional<int> MaxRaySteps;
        std::optional<GPP::Units::Precision> GpuPrecision;
        std::optional<bool> PhysicallyAccurate;
        std::optional<int> MetricType;
    };

    template <typename T>
    class OneShot
    {
    public:
        void Set(T value)
        {
            std::scoped_lock lock(m_Mutex);
            m_Value = std::move(value);
        }

        [[nodiscard]] std::optional<T> Take()
        {
            std::scoped_lock lock(m_Mutex);
            return std::exchange(m_Value, std::nullopt);
        }

        void Restore(T value)
        {
            std::scoped_lock lock(m_Mutex);
            if (!m_Value) m_Value = std::move(value);
        }

        [[nodiscard]] bool Pending() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Value.has_value();
        }

    private:
        mutable std::mutex m_Mutex;
        std::optional<T> m_Value;
    };

    // Script component health published by the runtime owner and shown by the inspector.
    class ScriptStatusBoard
    {
    public:
        void Publish(std::vector<GPP::ScriptStatus> statuses)
        {
            std::scoped_lock lock(m_Mutex);
            m_Statuses = std::move(statuses);
        }

        // Empty when the instance is healthy or unknown.
        [[nodiscard]] std::string Error(const std::uint64_t entity, const std::size_t index) const
        {
            std::scoped_lock lock(m_Mutex);
            for (const auto& status : m_Statuses)
            {
                if (status.Entity == entity && status.Index == index) return status.Message;
            }
            return {};
        }

    private:
        mutable std::mutex m_Mutex;
        std::vector<GPP::ScriptStatus> m_Statuses;
    };

    class UiState final : public GPP::IService
    {
    public:
        ScriptStatusBoard ScriptStatuses;
        std::atomic<bool> GpuFloat64Supported{false};
        bool ShowCameraWindow = false;
        bool ShowSystemWindow = false;
        bool ShowSceneWindow = true;
        bool ShowDebugWindow = false;
        bool ShowViewportHud = false;
        bool ShowCameraGizmos = true;
        bool ShowSettingsWindow = false;
        bool ShowGeneralRelativityWindow = false;
        bool ShowScienceWindow = false;
        bool ShowAnimationGraphWindow = false;

        bool IntroActive = false;
        bool IntroEnabled = true;
        bool ViewportVisible = false;
        bool TutorialActive = false;
        int TutorialStep = 0;

        std::uint64_t SelectedEntityGuid = 0;
        GizmoOperation ActiveGizmoOperation = GizmoOperation::Translate;
        GizmoMode ActiveGizmoMode = GizmoMode::World;
        bool GizmoSnapEnabled = false;
        glm::dvec3 GizmoTranslateSnapM{1.0e3, 1.0e3, 1.0e3};
        float GizmoRotateSnapDegrees = 15.0f;
        float GizmoScaleSnap = 0.1f;

        std::string CurrentSceneName;
        std::string CurrentScenePath;
        OneShot<std::string> PendingLoadScenePath;
        OneShot<std::string> PendingLoadTemplatePath;
        std::atomic<bool> PendingNewScene{false};
        std::atomic<bool> PendingSnapshotForPlay{false};
        std::atomic<bool> PendingStopSimulation{false};
        // CLI --scene override
        std::optional<std::string> StartupScenePath;

        glm::dvec3 CameraPositionM{0.0, 20.0 * GPP::Units::kSolarGravitationalRadius,
                                   100.0 * GPP::Units::kSolarGravitationalRadius};
        float CameraYaw = -90.0f;
        float CameraPitch = 0.0f;
        float CameraFov = 60.0f;
        double CameraSpeedMs = 5.0 * GPP::Units::kSolarGravitationalRadius;
        float CameraMouseSensitivity = 0.1f;
        bool PossessSceneCamera = true;
        bool PreviewSceneCamera = false;
        bool SceneCameraActive = false;

        glm::dvec3 ViewPositionM{0.0, 20.0 * GPP::Units::kSolarGravitationalRadius,
                                 100.0 * GPP::Units::kSolarGravitationalRadius};
        glm::vec3 ViewFront{0.0f, 0.0f, -1.0f};
        glm::vec3 ViewUp{0.0f, 1.0f, 0.0f};
        float ViewFov = 60.0f;

        glm::vec2 ViewportScreenMin{0.0f};
        glm::vec2 ViewportScreenMax{0.0f};

        ImFont* IconFont = nullptr;

        OneShot<ExportRequest> PendingExport;
        bool ExportActive = false;
        glm::uvec2 ExportResolution{1920, 1080};
        float ExportProgress = 0.0f;
        std::string ExportStatus;
        bool ExitWhenExportDone = false;
        float ExportStepDelta = 0.0f;
        std::uint64_t ExportStepSerial = 0;

        RenderToggles Render;
        GPP::Units::Precision SimulationPrecision = GPP::Units::Precision::F64;
        float GravityMultiplier = 1.0f;
        float SimulationTickRate = 60.0f;
    };
}
