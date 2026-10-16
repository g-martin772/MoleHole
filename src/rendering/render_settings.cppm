module;
#include <yaml-cpp/yaml.h>
export module MoleHole:Rendering.Settings;

import std;
import GPP;
import :Rendering.Frame;

export namespace MoleHole
{
    // Order matches the shader's metric index
    enum class Spacetime : int { Schwarzschild = 0, Kerr = 1, ReissnerNordstrom = 2, KerrNewman = 3 };
    enum class Transport : int { Straight = 0, AnalyticLens = 1, Integrated = 2 };
    enum class DebugView : int { None = 0, StepCount = 1, Termination = 2 };

    // Every switch of the renderer. Declared once in RenderSettingRegistry(); the Render window, the scene's
    // `Render:` YAML, `--set key=value` and `render.set` are generated from that list.
    struct RenderSettings
    {
        Spacetime Solution = Spacetime::Kerr;

        Transport TransportMode = Transport::AnalyticLens;
        float RayStepSize = 0.01f;
        int MaxRaySteps = 100000;
        float AdaptiveStepRate = 0.8f;
        GPP::Units::Precision GpuPrecision = GPP::Units::Precision::F32;

        bool GravitationalRedshift = true;
        bool DopplerBeaming = true;

        bool AccretionDisk = true;
        bool AccretionDiskVolumetric = false;
        float AccDiskHeight = 0.2f;
        float AccDiskSpeed = 0.5f;
        float AccDiskNoiseScale = 1.0f;
        float AccDiskNoiseLOD = 5.0f;

        bool RenderBlackHoles = true;
        bool RenderSpheres = true;
        bool RenderMeshes = true;

        bool ThirdPerson = false;
        float ThirdPersonDistance = 10.0f;
        float ThirdPersonHeight = 3.0f;
        FrameMode LengthUnit = FrameMode::PrimaryBlackHole;

        float ResolutionScale = 100.0f;

        bool Fxaa = true;
        bool Bloom = true;
        float BloomThreshold = 0.6f;
        int BloomBlurPasses = 5;
        float BloomIntensity = 5.0f;
        bool BloomDebug = false;
        bool LensFlare = true;
        float LensFlareIntensity = 0.3f;
        float LensFlareThreshold = 2.0f;

        bool ShowGravityGrid = false;
        bool ShowPhysicsDebug = false;
        bool ShowObjectPaths = false;

        DebugView Debug = DebugView::None;

        [[nodiscard]] bool UsesBloomChain() const noexcept { return Bloom || LensFlare || BloomDebug; }
        [[nodiscard]] bool Lensing() const noexcept { return TransportMode != Transport::Straight; }
    };

    [[nodiscard]] const GPP::SettingRegistry<RenderSettings>& RenderSettingRegistry();

    // Settings whose selected option needs hardware this device lacks fall back to their default (then to option 0).
    // Returns one note per setting that was changed.
    std::vector<std::string> SanitizeForDevice(RenderSettings& settings, const GPP::GpuCapabilities& caps);

    struct RenderProfile
    {
        std::string Name;
        std::string Description;
        bool BuiltIn = false;
        // Built-in this profile was forked from (what is saved against)
        std::string Base;
        RenderSettings Settings;
    };

    enum class RenderContext : int { Edit = 0, Play = 1, Reference = 2, Export = 3 };
    constexpr int kRenderContextCount = 4;
    [[nodiscard]] std::string_view RenderContextName(RenderContext context) noexcept;
    [[nodiscard]] std::string_view RenderContextLabel(RenderContext context) noexcept;
    [[nodiscard]] std::optional<RenderContext> RenderContextFromName(std::string_view name) noexcept;

    [[nodiscard]] const std::vector<RenderProfile>& BuiltInRenderProfiles();
    [[nodiscard]] RenderSettings LegacyRenderSettings(const YAML::Node& legacyState);

    inline constexpr const char* kRenderExtensionKey = "Render";
    inline constexpr int kRenderExtensionVersion = 1;

    // The scene's `Render:` extension as YAML text, empty when it has none
    [[nodiscard]] std::string RenderExtensionText(const GPP::Scene& scene);
    // Empty text removes the extension
    void StoreRenderExtension(GPP::Scene& scene, const std::string& yaml);

    // The named bundles plus which one each context uses. Built-ins are read-only; editing one forks it.
    class RenderProfileSet
    {
    public:
        RenderProfileSet();

        [[nodiscard]] const std::vector<RenderProfile>& Profiles() const noexcept { return m_Profiles; }
        [[nodiscard]] const RenderProfile* Find(std::string_view name) const;
        [[nodiscard]] const std::string& ProfileName(RenderContext context) const noexcept
        {
            return m_Context[static_cast<std::size_t>(context)];
        }
        [[nodiscard]] const RenderProfile& ProfileFor(RenderContext context) const;
        [[nodiscard]] const RenderSettings& SettingsFor(RenderContext context) const { return ProfileFor(context).Settings; }

        bool Assign(RenderContext context, std::string_view profileName);
        // Writes into the context's profile; a built-in is forked first. Returns the profile that now holds it.
        const RenderProfile& Replace(RenderContext context, const RenderSettings& settings);
        // Copy of an existing profile under a fresh name, assigned to the context
        const RenderProfile& Duplicate(RenderContext context);
        bool Remove(std::string_view name);
        bool Rename(std::string_view name, std::string_view newName);
        void Add(RenderProfile profile);
        [[nodiscard]] std::string UniqueName(std::string_view wanted) const;

        // User profiles and context assignments only; built-ins are code
        [[nodiscard]] YAML::Node ToYaml() const;
        // Unknown keys and profiles are dropped; `warnings` collects what was skipped
        static RenderProfileSet FromYaml(const YAML::Node& node, std::vector<std::string>* warnings = nullptr);

    private:
        std::vector<RenderProfile> m_Profiles;
        std::array<std::string, kRenderContextCount> m_Context;
    };
}
