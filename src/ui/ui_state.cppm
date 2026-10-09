export module MoleHole:UI.State;

import std;
import glm;
import GPP;
import imgui;

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
    };

    class UiState final : public GPP::IService
    {
    public:
        bool ShowCameraWindow = false;
        bool ShowSystemWindow = false;
        bool ShowSceneWindow = true;
        bool ShowDebugWindow = false;
        bool ShowViewportHud = false;
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
        glm::vec3 GizmoTranslateSnap{1.0f, 1.0f, 1.0f};
        float GizmoRotateSnapDegrees = 15.0f;
        float GizmoScaleSnap = 0.1f;

        std::string CurrentSceneName;
        std::string CurrentScenePath;
        std::optional<std::string> PendingLoadScenePath;
        std::optional<std::string> PendingLoadTemplatePath;
        bool PendingNewScene = false;
        bool PendingSnapshotForPlay = false;
        bool PendingStopSimulation = false;
        // CLI --scene override
        std::optional<std::string> StartupScenePath;

        glm::vec3 CameraPosition{0.0f, 20.0f, 100.0f};
        float CameraYaw = -90.0f;
        float CameraPitch = 0.0f;
        float CameraFov = 60.0f;
        float CameraSpeed = 5.0f;
        float CameraMouseSensitivity = 0.1f;

        glm::vec2 ViewportScreenMin{0.0f};
        glm::vec2 ViewportScreenMax{0.0f};

        ImFont* IconFont = nullptr;

        std::optional<ExportRequest> PendingExport;
        bool ExportActive = false;
        glm::uvec2 ExportResolution{1920, 1080};
        float ExportProgress = 0.0f;
        std::string ExportStatus;
        bool ExitWhenExportDone = false;

        RenderToggles Render;
        float GravityMultiplier = 1.0f;
        // Simulation ticks per second. Independent of the UI and viewport frame rates: the simulation
        // thread runs at this rate however slowly the viewport renders (or the UI draws).
        float SimulationTickRate = 60.0f;
    };
}
