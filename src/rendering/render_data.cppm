module;
#include <cstddef>
export module MoleHole:Rendering.Data;

import std;
import glm;
import GPP;
import :Simulation.Components;
import :Rendering.Metric;
import :Rendering.DiskPhase;
import :Rendering.Frame;
import :Rendering.Settings;
import :Physics.Observer;
import :Simulation.ObserverReadout;

export namespace MoleHole
{
    struct BlackHoleGpuData
    {
        glm::vec3 Position{0.0f}; float Mass{0.0f};
        // Spin and Charge are |a|/M and |Q|/M; after FillMetricData they hold the values of the active metric.
        glm::vec3 SpinAxis{0.0f, 1.0f, 0.0f}; float Spin{0.0f};
        float Charge{0.0f}; float HorizonRadius{0.0f}; float IscoRadius{0.0f}; float MassSolar{0.0f};
    };
    static_assert(sizeof(BlackHoleGpuData) == 48);

    struct SphereGpuData
    {
        glm::vec3 Position{0.0f}; float Radius{0.0f};
        glm::vec4 Color{1.0f};
        // Emissive 1 = a Star: Color is the radiance at the disc centre in image units, LimbDarkening its u
        float Mass{0.0f}; float Emissive{0.0f}; float LimbDarkening{0.0f}; float _Pad2{0.0f};
    };
    static_assert(sizeof(SphereGpuData) == 48);

    constexpr int kMaxBlackHoles = 8;
    // The sphere table of the trace uniform block grows every offset behind it by 48 bytes per sphere over the original 16;
    // the shaders' u_spheres arrays (trace_common.glsl, gravity_grid_data.glsl) must match.
    constexpr int kMaxSpheres = 64;
    constexpr std::size_t kSphereTableGrowth = (kMaxSpheres - 16) * 48;
    constexpr int kDiskProfileVec4s = kDiskProfileSamples / 4;
    // Outer edge of the disk in M; a free (artistic) parameter, nothing in the metric sets it.
    constexpr double kDiskOuterRadiusM = 20.0;

    struct RaytraceParamsGpu
    {
        glm::vec3 CameraPos{0.0f}; float Fov{60.0f};
        glm::vec3 CameraFront{0.0f, 0.0f, -1.0f}; float Aspect{1.0f};
        glm::vec3 CameraUp{0.0f, 1.0f, 0.0f}; float FlatPhase{0.0f};
        glm::vec3 CameraRight{1.0f, 0.0f, 0.0f}; float FlatPhaseFast{0.0f};

        std::int32_t EnableThirdPerson{0};
        float ThirdPersonDistance{5.0f};
        float ThirdPersonHeight{2.0f};
        float CubeSize{1.0f};

        std::int32_t IsPhysicallyAccurate{0};
        std::int32_t DebugMode{0};
        std::int32_t MetricType{1};
        std::int32_t GravitationalLensingEnabled{1};

        std::int32_t AccretionDiskEnabled{1};
        std::int32_t AccretionDiskVolumetric{0};
        std::int32_t RenderBlackHoles{1};
        std::int32_t RenderSpheres{1};

        std::int32_t GravitationalRedshiftEnabled{1};
        float AccDiskHeight{0.2f};
        float AccDiskNoiseScale{1.0f};
        float AccDiskNoiseLOD{5.0f};

        float AccDiskSpeed{0.5f};
        float DopplerBeamingEnabled{1.0f};
        float LutTempMin{1000.0f};
        float LutTempMax{40000.0f};

        float LutRedshiftMin{0.1f};
        float LutRedshiftMax{3.0f};
        float RayStepSize{0.01f};
        std::int32_t MaxRaySteps{100000};

        float AdaptiveStepRate{0.8f};
        std::int32_t NumBlackHoles{0};
        std::int32_t NumSpheres{0};
        float _Pad0{0.0f};

        std::array<BlackHoleGpuData, kMaxBlackHoles> BlackHoles{};
        std::array<SphereGpuData, kMaxSpheres> Spheres{};

        glm::mat4 ViewProjection{1.0f};

        glm::vec4 MeshBounds{0.0f};
        std::int32_t NumMeshInstances{0};
        float _MeshPad0{0.0f};
        float JitterX{0.0f};
        float JitterY{0.0f};

        float DiskAccretionRate{0.1f};
        float DiskOpacity{2.0f};
        float DiskDisplayTemperature{0.0f};
        std::int32_t DiskHotSpots{3};

        // Per black hole: x = peak temperature [K], y = inner radius (ISCO), z = outer radius, both in render units,
        // w = exposure reference temperature [K] (DiskExposureReference)
        std::array<glm::vec4, kMaxBlackHoles> DiskInfo{};
        // Per black hole kDiskProfileSamples values of T / peak, log-spaced between DiskInfo.y and DiskInfo.z
        std::array<glm::vec4, kMaxBlackHoles * kDiskProfileVec4s> DiskProfile{};
        // Per black hole kDiskPhaseBins bins of the gas rotation angle, see DiskPhaseTable
        std::array<glm::vec4, kMaxBlackHoles * kDiskPhaseBins> DiskPhase{};

        // Camera velocity relative to the ZAMO of the first hole along (e_r, e_theta, e_phi), in c; w = 1 unless
        // the camera is that ZAMO. Set by FillObserverData.
        glm::vec4 ObserverVelocity{0.0f};

        // Characteristic radii of the first hole for the integrated tracer, which draws them where rays cross the
        // equatorial plane (lensed like the disk). In render units: x/y photon orbit pro/retro, z/w marginally bound
        // pro/retro; OrbitRadii2 x/y ISCO pro/retro, z = 1 when the overlay is on. Set by FillOrbitOverlayData.
        glm::vec4 OrbitRadii{0.0f};
        glm::vec4 OrbitRadii2{0.0f};

        // Lensed trails (shaders/trails.glsl): grid half-size (render units), cells per axis, 1 = active, half-width (rad)
        glm::vec4 TrailGrid{0.0f};
        glm::vec4 TrailGrid2{0.0f}; // x = hash table size of the occupied trail cells
    };
    constexpr std::size_t kObserverVelocityOffset = 3616 + kSphereTableGrowth + 16 * kMaxBlackHoles * kDiskPhaseBins;
    static_assert(sizeof(RaytraceParamsGpu) == kObserverVelocityOffset + 80);
    static_assert(offsetof(RaytraceParamsGpu, DiskPhase) == 3616 + kSphereTableGrowth);
    static_assert(offsetof(RaytraceParamsGpu, ObserverVelocity) == kObserverVelocityOffset);
    static_assert(offsetof(RaytraceParamsGpu, OrbitRadii) == kObserverVelocityOffset + 16);
    static_assert(offsetof(RaytraceParamsGpu, DiskAccretionRate) == 1424 + kSphereTableGrowth);
    static_assert(offsetof(RaytraceParamsGpu, DiskInfo) == 1440 + kSphereTableGrowth);
    static_assert(offsetof(RaytraceParamsGpu, DiskProfile) == 1568 + kSphereTableGrowth);
    static_assert(offsetof(RaytraceParamsGpu, BlackHoles) == 176);
    static_assert(offsetof(RaytraceParamsGpu, Spheres) == 560);
    static_assert(offsetof(RaytraceParamsGpu, ViewProjection) == 1328 + kSphereTableGrowth);
    static_assert(offsetof(RaytraceParamsGpu, MeshBounds) == 1392 + kSphereTableGrowth);

    // A negative spin a* is a hole spinning the other way: the metric only sees |a*|, the axis is reversed.
    [[nodiscard]] inline glm::vec3 EffectiveSpinAxis(const glm::vec3& axis, const double spin)
    {
        const glm::vec3 unit = glm::length(axis) > 0.0f ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
        return spin < 0.0 ? -unit : unit;
    }

    // The entities FillSceneData uploads as spheres, in its order: the Stars first, then the other Spheres (a Star that
    // also has a Sphere counts once, as a Star). Entry i of every per-sphere table (BodyGpu, the local frame choice)
    // belongs to u_spheres[i], so they all take their order from here.
    [[nodiscard]] inline std::vector<entt::entity> UploadedSpheres(const GPP::Scene& scene)
    {
        std::vector<entt::entity> result;
        const auto& registry = scene.Registry();
        for (auto [entity, star, transform] : registry.view<const StarComponent, const GPP::TransformComponent>().each())
        {
            if (result.size() >= static_cast<std::size_t>(kMaxSpheres)) return result;
            result.push_back(entity);
        }
        for (auto [entity, sphere, transform] : registry.view<const SphereComponent, const GPP::TransformComponent>().each())
        {
            if (result.size() >= static_cast<std::size_t>(kMaxSpheres)) return result;
            if (!registry.all_of<StarComponent>(entity)) result.push_back(entity);
        }
        return result;
    }

    // Holes are uploaded heaviest first: the lensing engines treat index 0 as the primary hole.
    inline void FillSceneData(RaytraceParamsGpu& params, const GPP::Scene& scene, const RenderFrame& frame)
    {
        std::vector<BlackHoleGpuData> holes;
        for (auto [entity, blackHole, transform] :
             scene.Registry().view<const BlackHoleComponent, const GPP::TransformComponent>().each())
        {
            BlackHoleGpuData gpu;
            gpu.Position = frame.ToRender(transform.Position);
            gpu.Mass = frame.GeometricMass(blackHole.Mass);
            // Spin, charge and near-extremal gap resolved here, where the hole's spin range is known; FillMetricData
            // only applies the metric type. A negative a* reverses the axis.
            const auto metric = BlackHoleMetric(blackHole);
            gpu.SpinAxis = EffectiveSpinAxis(blackHole.SpinAxis, BlackHoleSignedSpin(blackHole));
            gpu.Spin = static_cast<float>(metric.SpinParameter);
            gpu.Charge = static_cast<float>(metric.Charge);
            gpu.MassSolar = static_cast<float>(blackHole.Mass / GPP::Units::kSolarMass);
            holes.push_back(gpu);
        }
        std::ranges::stable_sort(holes, std::greater<>{}, &BlackHoleGpuData::MassSolar);
        params.NumBlackHoles = static_cast<std::int32_t>(std::min<std::size_t>(holes.size(), kMaxBlackHoles));
        for (std::int32_t i = 0; i < params.NumBlackHoles; ++i) params.BlackHoles[static_cast<std::size_t>(i)] = holes[static_cast<std::size_t>(i)];

        params.NumSpheres = 0;
        for (auto [entity, star, transform] :
             scene.Registry().view<const StarComponent, const GPP::TransformComponent>().each())
        {
            if (params.NumSpheres >= kMaxSpheres) break;
            auto& gpu = params.Spheres[static_cast<std::size_t>(params.NumSpheres)];
            gpu.Position = frame.ToRender(transform.Position);
            gpu.Radius = frame.ToRenderLength(star.Radius);
            gpu.Color = glm::vec4(StarColor(star) * static_cast<float>(StarImageRadiance(star)), 1.0f);
            gpu.Mass = 0.0f;
            gpu.Emissive = 1.0f;
            gpu.LimbDarkening = std::clamp(star.LimbDarkening, 0.0f, 1.0f);
            ++params.NumSpheres;
        }
        for (auto [entity, sphere, transform] :
             scene.Registry().view<const SphereComponent, const GPP::TransformComponent>().each())
        {
            if (params.NumSpheres >= kMaxSpheres) break;
            if (scene.Registry().all_of<StarComponent>(entity)) continue;
            auto& gpu = params.Spheres[static_cast<std::size_t>(params.NumSpheres)];
            gpu.Position = frame.ToRender(transform.Position);
            gpu.Radius = frame.ToRenderLength(sphere.Radius);
            gpu.Color = glm::vec4(sphere.Color, 1.0f);
            gpu.Mass = 0.0f;
            if (const auto* rigidBody = scene.Registry().try_get<const GPP::RigidBodyComponent>(entity))
            {
                gpu.Mass = static_cast<float>(rigidBody->Mass / GPP::Units::kSolarMass);
            }
            ++params.NumSpheres;
        }
    }

    // What FillMetricData derives per black hole; recomputed only when one of its inputs changes.
    struct HoleMetricResult
    {
        float Spin{0.0f}, Charge{0.0f}, HorizonRadius{0.0f}, IscoRadius{0.0f};
        glm::vec4 DiskInfo{0.0f};
        std::array<glm::vec4, kDiskProfileVec4s> DiskProfile{};
    };

    struct MetricCache
    {
        struct Entry
        {
            std::array<float, 5> Key{};
            std::int32_t Metric{-1};
            bool Valid{false};
            HoleMetricResult Result;
        };
        std::array<Entry, kMaxBlackHoles> Entries{};
        int Misses{0}; // recomputations so far
    };

    [[nodiscard]] inline HoleMetricResult ComputeHoleMetric(const RaytraceParamsGpu& params, const BlackHoleGpuData& hole)
    {
        HoleMetricResult out;
        const auto metric = MakeKerrNewmanParams(params.MetricType, hole.Mass, hole.Spin, hole.Charge);
        const double mass = metric.Mass > 0.0 ? metric.Mass : 1.0;
        out.Spin = static_cast<float>(metric.SpinParameter / mass);
        out.Charge = static_cast<float>(metric.Charge / mass);
        out.HorizonRadius = static_cast<float>(CaptureRadius(metric));
        out.IscoRadius = static_cast<float>(ProgradeIscoRadius(metric));

        const auto disk = BuildDiskProfile(metric, hole.MassSolar, params.DiskAccretionRate, kDiskOuterRadiusM * metric.Mass);
        out.DiskInfo = glm::vec4(static_cast<float>(disk.PeakTemperature), static_cast<float>(disk.InnerRadius),
                                 static_cast<float>(disk.OuterRadius), static_cast<float>(DiskExposureReference(disk)));
        for (int v = 0; v < kDiskProfileVec4s; ++v)
        {
            for (int c = 0; c < 4; ++c) out.DiskProfile[static_cast<std::size_t>(v)][c] = disk.Temperature[static_cast<std::size_t>(v * 4 + c)];
        }
        return out;
    }

    // Applies params.MetricType to every black hole and precomputes what the shaders need from the metric:
    // horizon (or capture radius), ISCO and the Page–Thorne disk temperature profile (params.DiskAccretionRate
    // must be set), so root-finding and integrals stay on the CPU. Results are cached per hole.
    inline void FillMetricData(RaytraceParamsGpu& params, MetricCache& cache)
    {
        for (int i = 0; i < params.NumBlackHoles; ++i)
        {
            const auto index = static_cast<std::size_t>(i);
            auto& gpu = params.BlackHoles[index];
            auto& entry = cache.Entries[index];
            const std::array<float, 5> key{gpu.Mass, gpu.Spin, gpu.Charge, gpu.MassSolar, params.DiskAccretionRate};
            if (!entry.Valid || entry.Metric != params.MetricType || entry.Key != key)
            {
                entry.Result = ComputeHoleMetric(params, gpu);
                entry.Key = key;
                entry.Metric = params.MetricType;
                entry.Valid = true;
                ++cache.Misses;
            }

            const auto& result = entry.Result;
            gpu.Spin = result.Spin;
            gpu.Charge = result.Charge;
            gpu.HorizonRadius = result.HorizonRadius;
            gpu.IscoRadius = result.IscoRadius;
            params.DiskInfo[index] = result.DiskInfo;
            for (int v = 0; v < kDiskProfileVec4s; ++v)
            {
                params.DiskProfile[index * kDiskProfileVec4s + static_cast<std::size_t>(v)] = result.DiskProfile[static_cast<std::size_t>(v)];
            }
        }
    }

    inline void FillMetricData(RaytraceParamsGpu& params)
    {
        MetricCache cache;
        FillMetricData(params, cache);
    }

    // Disk gas rotation at coordinate time `simTime` [s]; `rate` multiplies the clock (the disk.speed preview).
    // Call after FillMetricData: the table follows the radii and the metric parameters the shader sees.
    inline void FillDiskPhases(RaytraceParamsGpu& params, const double simTime, const double rate)
    {
        params.FlatPhase = 0.0f;
        params.FlatPhaseFast = 0.0f;
        for (int i = 0; i < params.NumBlackHoles; ++i)
        {
            const auto index = static_cast<std::size_t>(i);
            const auto& gpu = params.BlackHoles[index];
            const auto& info = params.DiskInfo[index];
            if (!(gpu.Mass > 0.0f) || !(info.x > 0.0f)) continue;

            const KerrNewmanParams unit{.Mass = 1.0, .SpinParameter = gpu.Spin, .Charge = gpu.Charge};
            const double massKg = static_cast<double>(gpu.MassSolar) * GPP::Units::kSolarMass;
            const double gravitationalTime = GPP::Units::GravitationalRadius(massKg) / GPP::Units::kSpeedOfLight;
            const auto table = BuildDiskPhaseTable(unit, gravitationalTime, static_cast<double>(info.y) / gpu.Mass,
                                                   static_cast<double>(info.z) / gpu.Mass, simTime, rate);
            std::ranges::copy(table.Bins, params.DiskPhase.begin() + static_cast<std::ptrdiff_t>(index) * kDiskPhaseBins);
            if (i == 0)
            {
                params.FlatPhase = static_cast<float>(table.InnerEdgePhase);
                params.FlatPhaseFast = static_cast<float>(std::fmod(1.7 * table.InnerEdgePhase, 2.0 * std::numbers::pi));
            }
        }
    }

    // Notes about the scene that change how far the picture can be trusted; shown over the viewport.
    [[nodiscard]] inline std::vector<std::string> CollectRenderWarnings(const GPP::Scene& scene, const Spacetime solution)
    {
        std::vector<std::string> warnings;
        int holes = 0;
        bool naked = false;
        for (auto [entity, blackHole] : scene.Registry().view<const BlackHoleComponent>().each())
        {
            ++holes;
            const auto hole = BlackHoleMetric(blackHole);
            naked = naked || !HasHorizon(MakeKerrNewmanParams(static_cast<std::int32_t>(solution), 1.0, hole.SpinParameter, hole.Charge));
        }
        if (holes > 1)
            warnings.push_back(std::format("{} black holes: only the heaviest is lensed exactly (approximation)", holes));
        if (naked) warnings.emplace_back("Naked singularity: not physical (no horizon, no shadow)");
        return warnings;
    }

    // Boyer–Lindquist (r, theta) of a point relative to a hole with spin axis `spinAxis`; mirrors cartesianToBL in
    // shaders/kerr.glsl (oblate spheroidal coordinates for a != 0).
    [[nodiscard]] inline glm::dvec2 BoyerLindquistRadiusTheta(const glm::dvec3& relative, const glm::dvec3& spinAxis,
                                                              const double a)
    {
        const double height = glm::dot(relative, glm::normalize(spinAxis));
        const double w = glm::dot(relative, relative) - a * a;
        const double r = std::sqrt(std::max(0.5 * (w + std::sqrt(w * w + 4.0 * a * a * height * height)), 1e-12));
        return {r, std::acos(std::clamp(height / r, -1.0, 1.0))};
    }

    // Camera observer of the integrated transport: its velocity relative to the ZAMO of the first hole at the camera
    // position. Call after FillMetricData. Falls back to the ZAMO (w = 0) where the
    // chosen observer cannot exist.
    inline void FillObserverData(RaytraceParamsGpu& params, const ObserverKind kind, const OrbitDirection direction)
    {
        params.ObserverVelocity = glm::vec4(0.0f);
        if (kind == ObserverKind::Zamo || params.NumBlackHoles == 0) return;
        const auto& hole = params.BlackHoles[0];
        const auto metric = MakeKerrNewmanParams(params.MetricType, hole.Mass, hole.Spin, hole.Charge);
        const auto bl = BoyerLindquistRadiusTheta(glm::dvec3(params.CameraPos - hole.Position), glm::dvec3(hole.SpinAxis),
                                                  metric.SpinParameter);
        const auto velocity = ObserverVelocity(metric, kind, bl.x, bl.y, direction);
        if (!velocity) return;
        params.ObserverVelocity = glm::vec4(static_cast<float>((*velocity)[0]), static_cast<float>((*velocity)[1]),
                                            static_cast<float>((*velocity)[2]), 1.0f);
    }

    // Camera riding a Geodesic Body (the possessed scene camera): its velocity relative to the ZAMO of the first hole
    // replaces the preset observer. A body that cannot provide one (captured, following another hole) gives the ZAMO,
    // as ComputeObserverReadout reports. Returns false when `guid` is no Geodesic Body, so the preset applies.
    inline bool FillObserverFromBody(RaytraceParamsGpu& params, const GPP::Scene& scene, const std::uint64_t guid)
    {
        if (guid == 0) return false;
        const auto entity = scene.FindByGuid(guid);
        if (!scene.IsValid(entity) || !scene.Registry().all_of<GeodesicBodyComponent>(entity)) return false;
        params.ObserverVelocity = glm::vec4(0.0f);
        if (const auto velocity = BodyObserverVelocity(scene, guid))
        {
            params.ObserverVelocity = glm::vec4(static_cast<float>((*velocity)[0]), static_cast<float>((*velocity)[1]),
                                                static_cast<float>((*velocity)[2]), 1.0f);
        }
        return true;
    }

    // Ring radii for the in-tracer characteristic-radii overlay, in the metric the tracer integrates (after
    // FillMetricData). Disabled (z = 0) when the overlay is off or there is no hole.
    inline void FillOrbitOverlayData(RaytraceParamsGpu& params, const bool enabled)
    {
        params.OrbitRadii = glm::vec4(0.0f);
        params.OrbitRadii2 = glm::vec4(0.0f);
        if (!enabled || params.NumBlackHoles == 0) return;
        const auto& hole = params.BlackHoles[0];
        const auto m = MakeKerrNewmanParams(params.MetricType, hole.Mass, hole.Spin, hole.Charge);
        const auto f = [](const double r) { return static_cast<float>(r); };
        params.OrbitRadii = glm::vec4(f(PhotonOrbitRadius(m, OrbitDirection::Prograde)), f(PhotonOrbitRadius(m, OrbitDirection::Retrograde)),
                                      f(MarginallyBoundRadius(m, OrbitDirection::Prograde)),
                                      f(MarginallyBoundRadius(m, OrbitDirection::Retrograde)));
        params.OrbitRadii2 = glm::vec4(f(IscoRadius(m, OrbitDirection::Prograde)), f(IscoRadius(m, OrbitDirection::Retrograde)), 1.0f, 0.0f);
    }

    struct GravityGridPointGpu
    {
        glm::vec3 Position{0.0f}; float Mass{0.0f};
    };
    static_assert(sizeof(GravityGridPointGpu) == 16);

    constexpr int kMaxGravityGridMeshes = 8;

    struct GravityGridParamsGpu
    {
        glm::mat4 ViewProjection{1.0f};

        float PlaneY{-5.0f};
        float CellSize{2.0f};
        float LineThickness{0.03f};
        float Opacity{0.7f};

        glm::vec3 Color{0.1f, 0.1f, 0.8f};
        std::int32_t NumBlackHoles{0};

        std::int32_t NumSpheres{0};
        std::int32_t NumMeshes{0};
        float _Pad0{0.0f};
        float _Pad1{0.0f};

        std::array<GravityGridPointGpu, kMaxBlackHoles> BlackHoles{};
        std::array<GravityGridPointGpu, kMaxSpheres> Spheres{};
        std::array<GravityGridPointGpu, kMaxGravityGridMeshes> Meshes{};
    };
    constexpr std::size_t kGravityGridSphereGrowth = (kMaxSpheres - 16) * 16;
    static_assert(sizeof(GravityGridParamsGpu) == 624 + kGravityGridSphereGrowth);
    static_assert(offsetof(GravityGridParamsGpu, BlackHoles) == 112);
    static_assert(offsetof(GravityGridParamsGpu, Spheres) == 240);
    static_assert(offsetof(GravityGridParamsGpu, Meshes) == 496 + kGravityGridSphereGrowth);

    inline void FillGravityGridData(GravityGridParamsGpu& params, const GPP::Scene& scene, const RenderFrame& frame)
    {
        params.NumBlackHoles = 0;
        for (auto [entity, blackHole, transform] :
             scene.Registry().view<const BlackHoleComponent, const GPP::TransformComponent>().each())
        {
            if (params.NumBlackHoles >= kMaxBlackHoles) break;
            params.BlackHoles[static_cast<std::size_t>(params.NumBlackHoles)] =
                GravityGridPointGpu{.Position = frame.ToRender(transform.Position), .Mass = frame.GeometricMass(blackHole.Mass)};
            ++params.NumBlackHoles;
        }

        params.NumSpheres = 0;
        for (auto [entity, sphere, transform] :
             scene.Registry().view<const SphereComponent, const GPP::TransformComponent>().each())
        {
            if (params.NumSpheres >= kMaxSpheres) break;
            float mass = 0.0f;
            if (const auto* rigidBody = scene.Registry().try_get<const GPP::RigidBodyComponent>(entity))
            {
                mass = frame.GeometricMass(rigidBody->Mass);
            }
            params.Spheres[static_cast<std::size_t>(params.NumSpheres)] =
                GravityGridPointGpu{.Position = frame.ToRender(transform.Position), .Mass = mass};
            ++params.NumSpheres;
        }

        params.NumMeshes = 0;
        for (auto [entity, mesh, transform] :
             scene.Registry().view<const GPP::MeshComponent, const GPP::TransformComponent>().each())
        {
            if (params.NumMeshes >= kMaxGravityGridMeshes) break;
            float mass = 0.0f;
            if (const auto* rigidBody = scene.Registry().try_get<const GPP::RigidBodyComponent>(entity))
            {
                mass = frame.GeometricMass(rigidBody->Mass);
            }
            params.Meshes[static_cast<std::size_t>(params.NumMeshes)] =
                GravityGridPointGpu{.Position = frame.ToRender(transform.Position), .Mass = mass};
            ++params.NumMeshes;
        }
    }

    struct MeshCameraParamsGpu
    {
        glm::mat4 View{1.0f};
        glm::mat4 Projection{1.0f};
        glm::vec3 CameraPos{0.0f}; std::int32_t ToneMapping{0};
    };
    static_assert(sizeof(MeshCameraParamsGpu) == 144);

    struct MeshPushConstantsGpu
    {
        glm::mat4 Model{1.0f};
        std::uint64_t ExtraAddress{0};
        std::uint32_t MaterialIndex{0};
        std::uint32_t _Pad0{0};
    };
    static_assert(sizeof(MeshPushConstantsGpu) == 80);
}
