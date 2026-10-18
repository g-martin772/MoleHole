module;
#include <cstddef>
export module MoleHole:Rendering.Data;

import std;
import glm;
import GPP;
import :Simulation.Components;
import :Rendering.Metric;
import :Rendering.Frame;
import :Physics.Observer;
import :Simulation.ObserverReadout;

export namespace MoleHole
{
    struct BlackHoleGpuData
    {
        glm::vec3 Position{0.0f}; float Mass{0.0f};
        // Spin and Charge are a/M and Q/M; after FillMetricData they hold the clamped values of the active metric.
        glm::vec3 SpinAxis{0.0f, 1.0f, 0.0f}; float Spin{0.0f};
        float Charge{0.0f}; float HorizonRadius{0.0f}; float IscoRadius{0.0f}; float MassSolar{0.0f};
    };
    static_assert(sizeof(BlackHoleGpuData) == 48);

    struct SphereGpuData
    {
        glm::vec3 Position{0.0f}; float Radius{0.0f};
        glm::vec4 Color{1.0f};
        float Mass{0.0f}; float _Pad0{0.0f}; float _Pad1{0.0f}; float _Pad2{0.0f};
    };
    static_assert(sizeof(SphereGpuData) == 48);

    constexpr int kMaxBlackHoles = 8;
    constexpr int kMaxSpheres = 16;
    constexpr int kDiskProfileVec4s = kDiskProfileSamples / 4;
    // Outer edge of the disk in M; a free (artistic) parameter, nothing in the metric sets it.
    constexpr double kDiskOuterRadiusM = 20.0;

    struct RaytraceParamsGpu
    {
        glm::vec3 CameraPos{0.0f}; float Fov{60.0f};
        glm::vec3 CameraFront{0.0f, 0.0f, -1.0f}; float Aspect{1.0f};
        glm::vec3 CameraUp{0.0f, 1.0f, 0.0f}; float Time{0.0f};
        glm::vec3 CameraRight{1.0f, 0.0f, 0.0f}; float _CameraRightPad{0.0f};

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
        float _MeshPad1{0.0f};
        float _MeshPad2{0.0f};

        float DiskAccretionRate{0.1f};
        float DiskOpacity{2.0f};
        float DiskDisplayTemperature{0.0f};
        std::int32_t DiskHotSpots{3};

        // Per black hole: x = peak temperature [K], y = inner radius (ISCO), z = outer radius, both in render units
        std::array<glm::vec4, kMaxBlackHoles> DiskInfo{};
        // Per black hole kDiskProfileSamples values of T / peak, log-spaced between DiskInfo.y and DiskInfo.z
        std::array<glm::vec4, kMaxBlackHoles * kDiskProfileVec4s> DiskProfile{};

        // Camera velocity relative to the ZAMO of the first hole along (e_r, e_theta, e_phi), in c; w = 1 unless
        // the camera is that ZAMO. Set by FillObserverData.
        glm::vec4 ObserverVelocity{0.0f};
    };
    static_assert(sizeof(RaytraceParamsGpu) == 3632);
    static_assert(offsetof(RaytraceParamsGpu, ObserverVelocity) == 3616);
    static_assert(offsetof(RaytraceParamsGpu, DiskAccretionRate) == 1424);
    static_assert(offsetof(RaytraceParamsGpu, DiskInfo) == 1440);
    static_assert(offsetof(RaytraceParamsGpu, DiskProfile) == 1568);
    static_assert(offsetof(RaytraceParamsGpu, BlackHoles) == 176);
    static_assert(offsetof(RaytraceParamsGpu, Spheres) == 560);
    static_assert(offsetof(RaytraceParamsGpu, ViewProjection) == 1328);
    static_assert(offsetof(RaytraceParamsGpu, MeshBounds) == 1392);

    inline void FillSceneData(RaytraceParamsGpu& params, const GPP::Scene& scene, const RenderFrame& frame)
    {
        params.NumBlackHoles = 0;
        for (auto [entity, blackHole, transform] :
             scene.Registry().view<const BlackHoleComponent, const GPP::TransformComponent>().each())
        {
            if (params.NumBlackHoles >= kMaxBlackHoles) break;
            auto& gpu = params.BlackHoles[static_cast<std::size_t>(params.NumBlackHoles)];
            gpu.Position = frame.ToRender(transform.Position);
            gpu.Mass = frame.GeometricMass(blackHole.Mass);
            gpu.SpinAxis = glm::length(blackHole.SpinAxis) > 0.0f
                               ? glm::normalize(blackHole.SpinAxis)
                               : glm::vec3(0.0f, 1.0f, 0.0f);
            // Clamped here, where the hole's spin range is known; FillMetricData only applies the metric type
            const auto metric = BlackHoleMetric(blackHole);
            gpu.Spin = static_cast<float>(metric.SpinParameter);
            gpu.Charge = static_cast<float>(metric.Charge);
            gpu.MassSolar = static_cast<float>(blackHole.Mass / GPP::Units::kSolarMass);
            ++params.NumBlackHoles;
        }

        params.NumSpheres = 0;
        for (auto [entity, sphere, transform] :
             scene.Registry().view<const SphereComponent, const GPP::TransformComponent>().each())
        {
            if (params.NumSpheres >= kMaxSpheres) break;
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

    // Applies params.MetricType to every black hole and precomputes what the shaders need from the metric
    // (spin and charge arrive already clamped to each hole's spin range by FillSceneData):
    // horizon, ISCO and the Page–Thorne disk temperature profile (params.DiskAccretionRate must be set),
    // so root-finding and integrals stay on the CPU.
    inline void FillMetricData(RaytraceParamsGpu& params)
    {
        for (int i = 0; i < params.NumBlackHoles; ++i)
        {
            const auto index = static_cast<std::size_t>(i);
            auto& gpu = params.BlackHoles[index];
            const auto metric = MakeKerrNewmanParams(params.MetricType, gpu.Mass, gpu.Spin, gpu.Charge,
                                                     kExtremalHorizonParameter);
            const double mass = metric.Mass > 0.0 ? metric.Mass : 1.0;
            gpu.Spin = static_cast<float>(metric.SpinParameter / mass);
            gpu.Charge = static_cast<float>(metric.Charge / mass);
            gpu.HorizonRadius = static_cast<float>(OuterHorizonRadius(metric));
            gpu.IscoRadius = static_cast<float>(ProgradeIscoRadius(metric));

            const auto disk = BuildDiskProfile(metric, gpu.MassSolar, params.DiskAccretionRate,
                                               kDiskOuterRadiusM * metric.Mass);
            params.DiskInfo[index] = glm::vec4(static_cast<float>(disk.PeakTemperature), static_cast<float>(disk.InnerRadius),
                                               static_cast<float>(disk.OuterRadius), 0.0f);
            for (int v = 0; v < kDiskProfileVec4s; ++v)
            {
                auto& packed = params.DiskProfile[index * kDiskProfileVec4s + static_cast<std::size_t>(v)];
                for (int c = 0; c < 4; ++c) packed[c] = disk.Temperature[static_cast<std::size_t>(v * 4 + c)];
            }
        }
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
    // position. Call after FillMetricData, which clamps spin and charge. Falls back to the ZAMO (w = 0) where the
    // chosen observer cannot exist.
    inline void FillObserverData(RaytraceParamsGpu& params, const ObserverKind kind, const OrbitDirection direction)
    {
        params.ObserverVelocity = glm::vec4(0.0f);
        if (kind == ObserverKind::Zamo || params.NumBlackHoles == 0) return;
        const auto& hole = params.BlackHoles[0];
        const auto metric = MakeKerrNewmanParams(params.MetricType, hole.Mass, hole.Spin, hole.Charge,
                                                 kExtremalHorizonParameter);
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
    static_assert(sizeof(GravityGridParamsGpu) == 624);
    static_assert(offsetof(GravityGridParamsGpu, BlackHoles) == 112);
    static_assert(offsetof(GravityGridParamsGpu, Spheres) == 240);
    static_assert(offsetof(GravityGridParamsGpu, Meshes) == 496);

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
        glm::vec3 CameraPos{0.0f}; float _Pad0{0.0f};
        glm::vec3 LightDir{1.0f}; float _Pad1{0.0f};
    };
    static_assert(sizeof(MeshCameraParamsGpu) == 160);

    struct MeshPushConstantsGpu
    {
        glm::mat4 Model{1.0f};
        glm::vec4 BaseColorFactor{1.0f};
        float MetallicFactor{1.0f};
        float RoughnessFactor{1.0f};
        std::int32_t HasBaseColorTexture{0};
        float _Pad0{0.0f};
    };
    static_assert(sizeof(MeshPushConstantsGpu) == 96);
}
