module;
#include <cstddef>
export module MoleHole:Rendering.Data;

import std;
import glm;
import GPP;
import :Simulation.Components;

export namespace MoleHole
{
    struct BlackHoleGpuData
    {
        glm::vec3 Position{0.0f}; float Mass{0.0f};
        glm::vec3 SpinAxis{0.0f, 1.0f, 0.0f}; float Spin{0.0f};
        float Charge{0.0f}; float _Pad0{0.0f}; float _Pad1{0.0f}; float _Pad2{0.0f};
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
    constexpr float kSolarMassKg = 1.989e30f;

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
    };
    static_assert(sizeof(RaytraceParamsGpu) == 1328);
    static_assert(offsetof(RaytraceParamsGpu, BlackHoles) == 176);
    static_assert(offsetof(RaytraceParamsGpu, Spheres) == 560);

    inline void FillSceneData(RaytraceParamsGpu& params, const GPP::Scene& scene)
    {
        params.NumBlackHoles = 0;
        for (auto [entity, blackHole, transform] :
             scene.Registry().view<const BlackHoleComponent, const GPP::TransformComponent>().each())
        {
            if (params.NumBlackHoles >= kMaxBlackHoles) break;
            auto& gpu = params.BlackHoles[static_cast<std::size_t>(params.NumBlackHoles)];
            gpu.Position = transform.Position;
            gpu.Mass = blackHole.Mass;
            gpu.SpinAxis = glm::length(blackHole.SpinAxis) > 0.0f
                               ? glm::normalize(blackHole.SpinAxis)
                               : glm::vec3(0.0f, 1.0f, 0.0f);
            gpu.Spin = blackHole.Spin;
            gpu.Charge = blackHole.Charge;
            ++params.NumBlackHoles;
        }

        params.NumSpheres = 0;
        for (auto [entity, sphere, transform] :
             scene.Registry().view<const SphereComponent, const GPP::TransformComponent>().each())
        {
            if (params.NumSpheres >= kMaxSpheres) break;
            auto& gpu = params.Spheres[static_cast<std::size_t>(params.NumSpheres)];
            gpu.Position = transform.Position;
            gpu.Radius = sphere.Radius;
            gpu.Color = glm::vec4(sphere.Color, 1.0f);
            gpu.Mass = 0.0f;
            if (const auto* rigidBody = scene.Registry().try_get<const GPP::RigidBodyComponent>(entity))
            {
                gpu.Mass = rigidBody->Mass / kSolarMassKg;
            }
            ++params.NumSpheres;
        }
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

    inline void FillGravityGridData(GravityGridParamsGpu& params, const GPP::Scene& scene)
    {
        params.NumBlackHoles = 0;
        for (auto [entity, blackHole, transform] :
             scene.Registry().view<const BlackHoleComponent, const GPP::TransformComponent>().each())
        {
            if (params.NumBlackHoles >= kMaxBlackHoles) break;
            params.BlackHoles[static_cast<std::size_t>(params.NumBlackHoles)] =
                GravityGridPointGpu{.Position = transform.Position, .Mass = blackHole.Mass};
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
                mass = rigidBody->Mass / kSolarMassKg;
            }
            params.Spheres[static_cast<std::size_t>(params.NumSpheres)] =
                GravityGridPointGpu{.Position = transform.Position, .Mass = mass};
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
                mass = rigidBody->Mass / kSolarMassKg;
            }
            params.Meshes[static_cast<std::size_t>(params.NumMeshes)] =
                GravityGridPointGpu{.Position = transform.Position, .Mass = mass};
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
