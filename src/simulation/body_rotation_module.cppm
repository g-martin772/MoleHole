export module MoleHole:Simulation.BodyRotation;

import std;
import GPP;
import :Simulation.Components;

// Turns every Surface (and Cloud Layer, relative to the surface) with a rotation period about its sphere's spin axis.
// A body runs on its own clock: a Geodesic Body advances by its proper time dτ = (dτ/dt) dt, everything else by
// simulation time.
export namespace MoleHole
{
    // Rotation angle (rad, in [0, 2π)) after `properSeconds` more of the body's time.
    [[nodiscard]] inline double AdvanceRotation(const double angle, const double properSeconds, const double period)
    {
        if (!(period > 0.0) || !std::isfinite(properSeconds)) return angle;
        constexpr double kTwoPi = 2.0 * std::numbers::pi;
        const double next = std::fmod(angle + kTwoPi * properSeconds / period, kTwoPi);
        return next < 0.0 ? next + kTwoPi : next;
    }

    class BodyRotationModule final : public GPP::ISimulationModule
    {
    public:
        // After the geodesic module, so the clock rate belongs to this tick's position
        [[nodiscard]] GPP::SimulationPhase Phase() const noexcept override { return GPP::SimulationPhase::PostSim; }
        [[nodiscard]] bool ReportsChanges() const noexcept override { return true; }

        void OnTick(GPP::Scene& scene, const float deltaTime) override
        {
            const double seconds = static_cast<double>(deltaTime) * scene.Metadata().TimeScale;
            auto& registry = scene.Registry();
            for (auto [entity, surface] : registry.view<SurfaceComponent>().each())
            {
                if (!(surface.RotationPeriod > 0.0)) continue;
                double rate = 1.0;
                if (const auto* body = registry.try_get<const GeodesicBodyComponent>(entity);
                    body && body->Status == GeodesicBodyStatus::FreeFall)
                {
                    rate = body->ClockRate;
                }
                surface.RotationAngle = AdvanceRotation(surface.RotationAngle, seconds * rate, surface.RotationPeriod);
                scene.MarkDirty(entity);
            }
            for (auto [entity, clouds] : registry.view<CloudLayerComponent>().each())
            {
                if (!(clouds.RotationPeriod > 0.0)) continue;
                double rate = 1.0;
                if (const auto* body = registry.try_get<const GeodesicBodyComponent>(entity);
                    body && body->Status == GeodesicBodyStatus::FreeFall)
                {
                    rate = body->ClockRate;
                }
                clouds.RotationAngle = AdvanceRotation(clouds.RotationAngle, seconds * rate, clouds.RotationPeriod);
                scene.MarkDirty(entity);
            }
        }
    };
}
