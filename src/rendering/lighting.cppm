module;
#include <cstddef>
export module MoleHole:Rendering.Lighting;

import std;
import glm;
import GPP;
import :Simulation.Components;
import :Rendering.Frame;
import :Rendering.Settings;
import :Rendering.Metric;
import :Rendering.Data;

// CPU side of the lighting of meshes: the light buffer the raster and traced mesh shaders read, the Star lights,
// the disk light and the default key light. Background, units and approximations: docs/notes/lighting-materials.md
export namespace MoleHole
{
    // Peak luminance disk.glsl gives the hottest gas (DISK_REFERENCE_LUMINANCE): the display calibration of the disk.
    inline constexpr double kDiskReferenceLuminance = 0.25;
    // Image value 1 is the radiance of the BT.2408 reference white: L_v = 203 cd/m^2.
    inline constexpr double kImageReferenceLuminance = GPP::Radiometry::kHdrReferenceWhite;
    inline constexpr std::uint32_t kLightFlagDiskLobe = 1u << 8;

    // Image value per W m^-2 sr^-1 of radiance in the 683 lm/W convention of the light buffer.
    [[nodiscard]] constexpr double RadianceToImage()
    {
        return GPP::Radiometry::kLuminousEfficacyMax / kImageReferenceLuminance;
    }

    // Irradiance of the default key light: one image unit, i.e. the old (1,1,1) light of radiance 1.
    [[nodiscard]] constexpr double DefaultKeyIrradiance()
    {
        return kImageReferenceLuminance / GPP::Radiometry::kLuminousEfficacyMax;
    }

    // std140, mirrored by MeshLightingParams in shaders/mesh_lighting.glsl
    struct MeshLightingParamsGpu
    {
        float RadianceToImage{static_cast<float>(MoleHole::RadianceToImage())};
        float LengthUnit{1.0f}; // metres per render unit
        float SkyRadiance{0.0f}; // W m^-2 sr^-1, uniform
        std::int32_t ShadowsEnabled{0};
        float ShadowBias{1e-3f}; // render units
        float _Pad0{0.0f};
        float _Pad1{0.0f};
        float _Pad2{0.0f};
    };
    static_assert(sizeof(MeshLightingParamsGpu) == 32);

    // L_Edd = 4 pi G M m_p c / sigma_T [W]
    [[nodiscard]] inline double EddingtonLuminosity(const double massKg)
    {
        using namespace GPP::Units;
        return 4.0 * static_cast<double>(Pi()) * kGravitationalConstant * massKg * kProtonMass * kSpeedOfLight
               / kThomsonCrossSection;
    }

    // Mdot [kg/s] of an Eddington ratio: Mdot = ratio L_Edd / (eta c^2)
    [[nodiscard]] inline double DiskAccretionRateKgPerS(const double massKg, const double eddingtonRatio, const double efficiency)
    {
        return std::max(eddingtonRatio, 0.0) * EddingtonLuminosity(massKg)
               / (efficiency * GPP::Units::kSpeedOfLight * GPP::Units::kSpeedOfLight);
    }

    // L_disk = eta Mdot c^2 [W]
    [[nodiscard]] inline double DiskLuminosityW(const double efficiency, const double accretionRateKgPerS)
    {
        return efficiency * accretionRateKgPerS * GPP::Units::kSpeedOfLight * GPP::Units::kSpeedOfLight;
    }

    struct DiskLightInfo
    {
        bool Valid{false};
        double Efficiency{0.0};
        double AccretionRateKgPerS{0.0};
        double LuminosityW{0.0};
        double PeakTemperatureK{0.0};
        double ColorTemperatureK{0.0};
        double InnerRadiusM{0.0};   // ISCO, where the emission starts
        double HorizonRadiusM{0.0};
        double Calibration{0.0};    // image value per W m^-2 sr^-1: the disk's peak radiance sigma T^4 / pi shows as 0.25
        glm::vec3 Color{1.0f};
        glm::dvec3 PositionM{0.0};
        glm::vec3 Axis{0.0f, 1.0f, 0.0f};
        float PeakIntensity{0.0f};  // W/sr along the spin axis in the light buffer's convention
    };

    // The Page-Thorne profile is expensive; it only depends on the hole's shape and the accretion rate.
    struct DiskLightCache
    {
        struct Entry
        {
            std::array<double, 5> Key{};
            double Efficiency{0.0};
            double PeakTemperatureK{0.0};
            double InnerRadiusG{0.0};
            double HorizonRadiusG{0.0};
            bool Valid{false};
        };
        Entry Hole;
        int Misses{0};
    };

    // Point source at the primary hole radiating the disk luminosity as a flat Lambertian emitter (I ~ |cos theta|
    // about the spin axis). Approximation L2, see the notes.
    [[nodiscard]] inline DiskLightInfo ComputeDiskLight(const BlackHoleComponent& hole, const glm::dvec3& positionM,
                                                        const RenderSettings& settings, DiskLightCache& cache)
    {
        DiskLightInfo out;
        using namespace GPP::Units;
        const auto physical = BlackHoleMetric(hole);
        const auto metric = MakeKerrNewmanParams(static_cast<std::int32_t>(settings.Solution), 1.0, physical.SpinParameter, physical.Charge);
        if (hole.Mass <= 0.0 || settings.DiskAccretionRate <= 0.0 || !HasHorizon(metric)) return out;

        const std::array<double, 5> key{hole.Mass, metric.SpinParameter, metric.Charge,
                                        static_cast<double>(settings.DiskAccretionRate),
                                        static_cast<double>(settings.Solution)};
        auto& entry = cache.Hole;
        if (!entry.Valid || entry.Key != key)
        {
            const auto profile = BuildDiskProfile(metric, hole.Mass / kSolarMass, settings.DiskAccretionRate,
                                                  kDiskOuterRadiusM * metric.Mass);
            entry.Key = key;
            entry.Efficiency = std::max(RadiativeEfficiency(metric), 1e-3);
            entry.PeakTemperatureK = profile.PeakTemperature;
            entry.InnerRadiusG = profile.InnerRadius;
            entry.HorizonRadiusG = CaptureRadius(metric);
            entry.Valid = true;
            ++cache.Misses;
        }
        if (!(entry.PeakTemperatureK > 0.0)) return out;

        const double gravitationalRadius = GravitationalRadius(hole.Mass);
        out.Efficiency = entry.Efficiency;
        out.AccretionRateKgPerS = DiskAccretionRateKgPerS(hole.Mass, settings.DiskAccretionRate, entry.Efficiency);
        out.LuminosityW = DiskLuminosityW(entry.Efficiency, out.AccretionRateKgPerS);
        out.PeakTemperatureK = entry.PeakTemperatureK;
        out.ColorTemperatureK = settings.DiskDisplayTemperature > 0.0f ? settings.DiskDisplayTemperature
                                                                       : entry.PeakTemperatureK;
        out.InnerRadiusM = entry.InnerRadiusG * gravitationalRadius;
        out.HorizonRadiusM = entry.HorizonRadiusG * gravitationalRadius;
        const double t2 = entry.PeakTemperatureK * entry.PeakTemperatureK;
        out.Calibration = kDiskReferenceLuminance * static_cast<double>(Pi()) / (kStefanBoltzmann * t2 * t2);
        out.Color = GPP::Radiometry::BlackbodyColor(out.ColorTemperatureK);
        out.PositionM = positionM;
        out.Axis = EffectiveSpinAxis(hole.SpinAxis, BlackHoleSignedSpin(hole));
        const double peakPhysical = out.LuminosityW / (2.0 * static_cast<double>(Pi()));
        out.PeakIntensity = static_cast<float>(peakPhysical * out.Calibration / RadianceToImage());
        out.Valid = true;
        return out;
    }

    struct MeshLighting
    {
        GPP::LightsGpu Lights;
        MeshLightingParamsGpu Params;
        bool DefaultKeyLight{false};
        int Stars{0};
        DiskLightInfo Disk;
    };

    // Scene lights, Stars and the disk light for the mesh shaders. Positions are metres relative to the frame origin.
    // A scene without any light or Star gets the default key light so unlit scenes keep their look.
    [[nodiscard]] inline MeshLighting BuildMeshLighting(const GPP::Scene& scene, const RenderFrame& frame,
                                                        const RenderSettings& settings, DiskLightCache& cache,
                                                        const bool shadowsAvailable)
    {
        MeshLighting out;
        out.Lights = GPP::BuildLightBuffer(scene, GPP::LightBuildOptions{.OriginM = frame.OriginM});
        auto& lights = out.Lights;
        const auto append = [&](const GPP::LightGpu& light)
        {
            if (lights.Count >= GPP::kMaxLights)
            {
                ++lights.Dropped;
                return;
            }
            lights.Lights[lights.Count++] = light;
            if (light.Kind == static_cast<std::uint32_t>(GPP::LightKind::Directional)) ++lights.DirectionalCount;
        };

        const auto& registry = scene.Registry();
        const bool sceneLights = lights.Count > 0;
        for (const auto [entity, star, transform] : registry.view<const StarComponent, const GPP::TransformComponent>().each())
        {
            const double luminosity = StarLuminosity(star);
            if (!(luminosity > 0.0) || IsHidden(registry, entity)) continue;
            GPP::LightGpu light;
            light.Kind = static_cast<std::uint32_t>(GPP::LightKind::Point);
            light.Position = glm::vec3(transform.Position - frame.OriginM);
            light.Radius = static_cast<float>(star.Radius);
            const double intensity = luminosity / (4.0 * static_cast<double>(GPP::Units::Pi())) * StarLuminousFraction(star);
            light.Color = StarColor(star) * static_cast<float>(intensity);
            light.Flux = static_cast<float>(luminosity);
            append(light);
            ++out.Stars;
        }

        if (settings.DiskLight)
        {
            const BlackHoleComponent* primary = nullptr;
            glm::dvec3 position{0.0};
            for (const auto [entity, hole, transform] :
                 registry.view<const BlackHoleComponent, const GPP::TransformComponent>().each())
            {
                if (IsHidden(registry, entity)) continue;
                if (!primary || hole.Mass > primary->Mass)
                {
                    primary = &hole;
                    position = transform.Position;
                }
            }
            if (primary)
            {
                out.Disk = ComputeDiskLight(*primary, position, settings, cache);
                if (out.Disk.Valid)
                {
                    GPP::LightGpu light;
                    light.Kind = static_cast<std::uint32_t>(GPP::LightKind::Point);
                    light.Position = glm::vec3(position - frame.OriginM);
                    light.Direction = out.Disk.Axis;
                    light.Color = out.Disk.Color * out.Disk.PeakIntensity;
                    light.Flux = static_cast<float>(out.Disk.LuminosityW);
                    light.Flags = kLightFlagDiskLobe;
                    light.HalfExtent = {static_cast<float>(out.Disk.InnerRadiusM), static_cast<float>(out.Disk.HorizonRadiusM)};
                    append(light);
                }
            }
        }

        if (!sceneLights && out.Stars == 0)
        {
            GPP::LightGpu key;
            key.Kind = static_cast<std::uint32_t>(GPP::LightKind::Directional);
            key.Direction = -glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
            key.Color = glm::vec3(static_cast<float>(DefaultKeyIrradiance()));
            key.CosOuter = 1.0f;
            // Directionals come first in the buffer: shift the rest up so the order stays deterministic.
            if (lights.Count < GPP::kMaxLights)
            {
                for (auto i = lights.Count; i > 0; --i) lights.Lights[i] = lights.Lights[i - 1];
                lights.Lights[0] = key;
                ++lights.Count;
                ++lights.DirectionalCount;
                out.DefaultKeyLight = true;
            }
        }

        out.Params.LengthUnit = static_cast<float>(frame.LengthUnitM);
        out.Params.SkyRadiance = settings.SkyRadiance;
        out.Params.ShadowsEnabled = shadowsAvailable && settings.Shadows ? 1 : 0;
        return out;
    }
}
