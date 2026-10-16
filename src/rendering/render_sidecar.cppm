module;
#include <yaml-cpp/yaml.h>
export module MoleHole:Rendering.Sidecar;

import std;
import glm;
import GPP;
import :Rendering.Settings;

export namespace MoleHole
{
    // Everything needed to reproduce an exported image
    struct RenderSidecarInfo
    {
        std::string OutputPath;
        bool Video = false;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        int Frames = 1;
        int Framerate = 0;

        std::string ScenePath;
        std::string SceneName;
        // Serialized scene, hashed so a changed scene is noticed
        std::string SceneYaml;

        std::string ProfileName;
        RenderSettings Settings;
        std::vector<std::pair<std::string, std::string>> Overrides;

        glm::dvec3 CameraPositionM{0.0};
        float CameraYaw = 0.0f;
        float CameraPitch = 0.0f;
        float CameraFov = 60.0f;

        std::string GitCommit;
        GPP::GpuCapabilities Gpu;
        double WallSeconds = 0.0;
        // GPU time of the last captured frame (what the viewport target measured)
        double GpuMs = 0.0;
        std::string Timestamp;
    };

    // FNV-1a 64 bit; stable across runs and platforms, unlike std::hash
    [[nodiscard]] std::uint64_t HashText(std::string_view text) noexcept;
    [[nodiscard]] std::string HashHex(std::string_view text);

    // Short commit of the checkout `repoRoot` belongs to; "unknown" when it is not a git checkout
    [[nodiscard]] std::string ReadGitCommit(const std::filesystem::path& repoRoot);

    [[nodiscard]] std::filesystem::path SidecarPathFor(const std::filesystem::path& output);
    [[nodiscard]] std::string BuildRenderSidecar(const RenderSidecarInfo& info);
    // False when the file could not be written
    bool WriteRenderSidecar(const std::filesystem::path& output, const RenderSidecarInfo& info);
}
