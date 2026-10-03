export module MoleHole:UI.State;

import std;
import glm;
import GPP;
import imgui;

export namespace MoleHole
{
    constexpr std::uint32_t kViewportBufferId = 1;

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
        int DebugMode = 0;
        float AccDiskHeight = 0.2f;
        float AccDiskSpeed = 0.5f;
        float AccDiskNoiseScale = 1.0f;
        float AccDiskNoiseLOD = 5.0f;
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

        std::uint64_t SelectedEntityGuid = 0;

        std::string CurrentSceneName;
        std::string CurrentScenePath;
        std::optional<std::string> PendingLoadScenePath;
        bool PendingNewScene = false;
        bool PendingStartPaused = false;

        glm::vec3 CameraPosition{0.0f, 20.0f, 100.0f};
        float CameraYaw = -90.0f;
        float CameraPitch = 0.0f;
        float CameraFov = 60.0f;
        float CameraSpeed = 5.0f;
        float CameraMouseSensitivity = 0.1f;

        glm::vec2 ViewportScreenMin{0.0f};
        glm::vec2 ViewportScreenMax{0.0f};

        ImFont* IconFont = nullptr;

        RenderToggles Render;
    };
}
