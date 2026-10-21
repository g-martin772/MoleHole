module;
#include <glm/glm.hpp>
export module MoleHole:Simulation.GeodesicBodies;

import std;
import GPP;
import :Simulation.Components;
import :Rendering.Metric;
import :Physics.Geodesic;
import :Physics.Observer;

// Moves GeodesicBodyComponent entities along timelike geodesics of their black hole's exact Kerr–Newman metric.
// Simulation time is the Boyer–Lindquist coordinate time t of the hole, the clock of a distant static observer;
// each body also accumulates its own proper time. Background: docs/notes/geodesic_bodies.md
export namespace MoleHole
{
    // A black hole as seen by the integrator: its metric in units of its mass, its spin frame and the SI scales of
    // one geometric length (r_g = GM/c²) and time (GM/c³).
    struct HoleFrame
    {
        KerrNewmanParams Metric{};
        glm::dvec3 Position{0.0};
        glm::dmat3 Frame{1.0}; // columns X, Y, Z with Z along the spin axis; world = Frame * local
        double LengthM{1.0};
        double TimeS{1.0};
    };

    [[nodiscard]] inline HoleFrame MakeHoleFrame(const BlackHoleComponent& hole, const glm::dvec3& position)
    {
        // Same frame as spinFrame in shaders/kerr.glsl, so bodies and the rendered disk agree on the orientation
        // Normalised in double: a float-normalised axis is only unit to ~1e-7 and the frame would not be orthonormal
        const glm::dvec3 unit = glm::length(hole.SpinAxis) > 0.0f ? glm::normalize(glm::dvec3(hole.SpinAxis))
                                                                  : glm::dvec3(0.0, 1.0, 0.0);
        const glm::dvec3 axis = BlackHoleSignedSpin(hole) < 0.0 ? -unit : unit;
        const glm::dvec3 helper = std::abs(axis.y) < 0.99 ? glm::dvec3(0.0, 1.0, 0.0) : glm::dvec3(1.0, 0.0, 0.0);
        const glm::dvec3 x = glm::normalize(glm::cross(helper, axis));
        const glm::dvec3 y = glm::cross(axis, x);
        return HoleFrame{
            .Metric = BlackHoleMetric(hole),
            .Position = position,
            .Frame = glm::dmat3(x, y, axis),
            .LengthM = GPP::Units::GravitationalRadius(hole.Mass),
            .TimeS = GPP::Units::GeometricTime(hole.Mass),
        };
    }

    // World position (m) -> Boyer–Lindquist (r, theta, phi) in units of M. Oblate spheroidal for a != 0, as
    // cartesianToBL in shaders/kerr.glsl.
    [[nodiscard]] inline glm::dvec3 WorldToBoyerLindquist(const HoleFrame& hole, const glm::dvec3& world)
    {
        const glm::dvec3 p = glm::transpose(hole.Frame) * (world - hole.Position) / hole.LengthM;
        const double a = hole.Metric.SpinParameter;
        const double w = glm::dot(p, p) - a * a;
        const double r = std::sqrt(std::max(0.5 * (w + std::sqrt(w * w + 4.0 * a * a * p.z * p.z)), 1e-24));
        return {r, std::acos(std::clamp(p.z / r, -1.0, 1.0)), std::atan2(p.y, p.x)};
    }

    [[nodiscard]] inline glm::dvec3 BoyerLindquistToWorld(const HoleFrame& hole, const double r, const double theta,
                                                          const double phi)
    {
        const double rho = std::sqrt(r * r + hole.Metric.SpinParameter * hole.Metric.SpinParameter);
        const glm::dvec3 local{rho * std::sin(theta) * std::cos(phi), rho * std::sin(theta) * std::sin(phi),
                               r * std::cos(theta)};
        return hole.Position + hole.Frame * local * hole.LengthM;
    }

    // The hole with this guid, or the first black hole in the scene for guid 0 (a Geodesic Body's default).
    [[nodiscard]] inline std::optional<HoleFrame> FindHoleFrame(const GPP::Scene& scene, const std::uint64_t guid)
    {
        const auto& registry = scene.Registry();
        if (guid != 0)
        {
            const auto entity = scene.FindByGuid(guid);
            if (!scene.IsValid(entity)) return std::nullopt;
            const auto* hole = registry.try_get<BlackHoleComponent>(entity);
            const auto* transform = registry.try_get<GPP::TransformComponent>(entity);
            if (!hole || !transform) return std::nullopt;
            return MakeHoleFrame(*hole, transform->Position);
        }
        for (auto [entity, hole, transform] : registry.view<const BlackHoleComponent, const GPP::TransformComponent>().each())
        {
            return MakeHoleFrame(hole, transform.Position);
        }
        return std::nullopt;
    }

    // Geodesic starting at `world` with the body's launch settings; nullopt where that start is impossible
    // (inside the horizon, static inside the ergosphere, orbiting inside the photon orbit, faster than light).
    [[nodiscard]] inline std::optional<GeodesicState<double>> LaunchGeodesic(const HoleFrame& hole, const glm::dvec3& world,
                                                                             const GeodesicBodyComponent& body)
    {
        const auto bl = WorldToBoyerLindquist(hole, world);
        std::optional<Vector3> velocity;
        if (body.Launch == GeodesicLaunch::Velocity)
        {
            velocity = Vector3{body.LaunchVelocity.x, body.LaunchVelocity.y, body.LaunchVelocity.z};
        }
        else
        {
            velocity = ObserverVelocity(hole.Metric, static_cast<ObserverKind>(body.Launch), bl.x, bl.y,
                                        body.Retrograde ? OrbitDirection::Retrograde : OrbitDirection::Prograde);
        }
        if (!velocity) return std::nullopt;
        return MakeGeodesicFromZamoVelocity<double>(hole.Metric, bl.x, bl.y, bl.z, *velocity);
    }

    class GeodesicSimulationModule final : public GPP::ISimulationModule
    {
    public:
        explicit GeodesicSimulationModule(std::shared_ptr<GPP::Logger> logger = nullptr) : m_Logger(std::move(logger)) {}

        // After PhysX, so the geodesic position wins; PhysX picks it up as a kinematic pose on the next tick.
        [[nodiscard]] GPP::SimulationPhase Phase() const noexcept override { return GPP::SimulationPhase::PostSim; }
        [[nodiscard]] bool ReportsChanges() const noexcept override { return true; }

        void OnInit(GPP::Scene& scene) override;
        void OnTick(GPP::Scene& scene, float deltaTime) override;

        void SetTolerance(const GeodesicTolerance& tolerance) noexcept { m_Tolerance = tolerance; }

    private:
        std::shared_ptr<GPP::Logger> m_Logger;
        GeodesicTolerance m_Tolerance{.Relative = 1e-10, .Absolute = 1e-12};
    };
}
