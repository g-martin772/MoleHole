module;
#include <glm/glm.hpp>
module MoleHole;

import :Simulation.GeodesicBodies;
import std;
import GPP;

namespace MoleHole
{
    namespace
    {
        // Identifies what a body was launched with, so editing the launch settings or the hole re-launches it.
        std::uint64_t LaunchKey(const GeodesicBodyComponent& body, const HoleFrame& hole)
        {
            std::uint64_t key = 1469598103934665603ull;
            const auto mixBits = [&](const std::uint64_t bits)
            {
                key ^= bits;
                key *= 1099511628211ull;
            };
            const auto mix = [&](const double value) { mixBits(std::bit_cast<std::uint64_t>(value)); };
            mixBits(body.BlackHole);
            mixBits(static_cast<std::uint64_t>(body.Launch));
            mix(body.Retrograde ? 1.0 : 0.0);
            mix(body.LaunchVelocity.x);
            mix(body.LaunchVelocity.y);
            mix(body.LaunchVelocity.z);
            mix(hole.Metric.SpinParameter);
            mix(hole.Metric.Charge);
            mix(hole.LengthM);
            for (int i = 0; i < 3; ++i) mix(hole.Position[i]);
            for (int i = 0; i < 3; ++i) mix(hole.Frame[2][i]);
            return key;
        }

        void UpdateReadouts(GeodesicBodyComponent& body, const HoleFrame& hole)
        {
            if (body.Status != GeodesicBodyStatus::FreeFall)
            {
                body.ClockRate = 0.0;
                return;
            }
            const auto velocity = ZamoVelocity(hole.Metric, body.State);
            body.Speed = std::sqrt(Dot3(velocity, velocity));
            body.ClockRate = ProperTimeRate(hole.Metric, body.State.R, body.State.Theta, velocity);
        }

        void Launch(GeodesicBodyComponent& body, const HoleFrame& hole, const glm::dvec3& position)
        {
            body.LaunchedFrom = LaunchKey(body, hole);
            body.WrittenPosition = position;
            body.ProperTime = 0.0;
            body.Speed = 0.0;
            body.ClockRate = 1.0;
            const auto state = LaunchGeodesic(hole, position, body);
            if (!state)
            {
                body.Status = GeodesicBodyStatus::CannotLaunch;
                return;
            }
            body.State = *state;
            body.Status = GeodesicBodyStatus::FreeFall;
            UpdateReadouts(body, hole);
        }
    }

    void GeodesicSimulationModule::OnInit(GPP::Scene& scene)
    {
        auto& registry = scene.Registry();
        for (auto [entity, body, transform] : registry.view<GeodesicBodyComponent, const GPP::TransformComponent>().each())
        {
            const auto hole = FindHoleFrame(scene, body.BlackHole);
            if (!hole)
            {
                body.Status = GeodesicBodyStatus::NoBlackHole;
                continue;
            }
            if (body.Status == GeodesicBodyStatus::NotLaunched) Launch(body, *hole, transform.Position);
        }
    }

    void GeodesicSimulationModule::OnTick(GPP::Scene& scene, const float deltaTime)
    {
        auto& registry = scene.Registry();
        // Runner seconds -> physical seconds, as PhysicsSimulationModule does
        const double seconds = static_cast<double>(deltaTime) * scene.Metadata().TimeScale;

        for (auto [entity, body, transform] : registry.view<GeodesicBodyComponent, GPP::TransformComponent>().each())
        {
            const auto hole = FindHoleFrame(scene, body.BlackHole);
            if (!hole)
            {
                body.Status = GeodesicBodyStatus::NoBlackHole;
                continue;
            }

            // Moved from outside (editor, script, undo) or launch settings edited: start again from here
            const double tolerance = 1e-9 * std::max(glm::length(transform.Position - hole->Position), hole->LengthM);
            if (body.Status == GeodesicBodyStatus::NotLaunched || body.Status == GeodesicBodyStatus::NoBlackHole
                || glm::length(transform.Position - body.WrittenPosition) > tolerance
                || body.LaunchedFrom != LaunchKey(body, *hole))
            {
                Launch(body, *hole, transform.Position);
                scene.MarkDirty(entity);
            }
            if (body.Status != GeodesicBodyStatus::FreeFall || !(seconds > 0.0)) continue;

            const double lambda = body.State.Lambda;
            AdvanceToCoordinateTime(hole->Metric, body.State, seconds / hole->TimeS, m_Tolerance);
            body.ProperTime += (body.State.Lambda - lambda) * hole->TimeS;
            if (body.State.Status == GeodesicStatus::Captured)
            {
                body.Status = GeodesicBodyStatus::Captured;
            }
            else if (body.State.Status != GeodesicStatus::Ok)
            {
                body.Status = GeodesicBodyStatus::CannotLaunch;
                if (m_Logger) m_Logger->Warn("GeodesicSimulationModule: integration failed; body stopped");
            }

            transform.Position = BoyerLindquistToWorld(*hole, body.State.R, body.State.Theta, body.State.Phi);
            body.WrittenPosition = transform.Position;
            UpdateReadouts(body, *hole);
            scene.MarkDirty(entity);
        }
    }
}
