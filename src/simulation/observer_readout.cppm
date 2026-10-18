module;
#include <glm/glm.hpp>
export module MoleHole:Simulation.ObserverReadout;

import std;
import GPP;
import :Simulation.Components;
import :Simulation.GeodesicBodies;
import :Rendering.Metric;
import :Physics.Geodesic;
import :Physics.Observer;

// What the camera's observer measures: the values behind the camera window's Observer section. The renderer uses
// the same rules (FillObserverFromBody, FillObserverData), so readout and image always describe the same observer.
// Background: docs/notes/observers.md, docs/notes/geodesic_bodies.md
export namespace MoleHole
{
    enum class ObserverSource : std::int32_t
    {
        None = 0,     // no black hole in the scene
        Preset = 1,   // the camera.observer render setting
        Body = 2,     // the scene camera rides a Geodesic Body
        Fallback = 3, // the requested observer cannot exist here; the view uses the ZAMO
    };

    struct ObserverReadout
    {
        ObserverSource Source{ObserverSource::None};
        ObserverKind Kind{ObserverKind::Zamo}; // the observer actually used (Zamo after a fallback)
        bool FreeFall{false};
        bool InsideHorizon{false};
        double Radius{0.0};          // Boyer–Lindquist r / M
        double Speed{0.0};           // relative to the local ZAMO, in c
        double Gamma{1.0};
        double ClockRate{1.0};       // dτ/dt against a distant static observer
        double AccelerationMs2{0.0}; // proper acceleration needed to keep this motion
        double ProperTimeS{0.0};     // geodesic bodies only
        bool Rendered{true};         // false when the transport ignores the observer (only Integrated renders it)
        std::string Status;
    };

    // "1.5 h", "3.2 days", "7.06 years": a duration in the largest unit that keeps it readable.
    [[nodiscard]] inline std::string FormatLongDuration(const double seconds)
    {
        if (!std::isfinite(seconds)) return "forever";
        const double year = GPP::Units::kJulianYear;
        const double day = GPP::Units::kDay;
        if (seconds >= year) return std::format("{:.3g} years", seconds / year);
        if (seconds >= day) return std::format("{:.3g} days", seconds / day);
        if (seconds >= 3600.0) return std::format("{:.3g} h", seconds / 3600.0);
        if (seconds >= 60.0) return std::format("{:.3g} min", seconds / 60.0);
        return std::format("{:.3g} s", seconds);
    }

    // Earth's surface gravity from GM⊕ / R⊕² (equatorial, without rotation), to show accelerations in g⊕.
    [[nodiscard]] inline double EarthSurfaceGravity()
    {
        return GPP::Units::kEarthMassParameter / (GPP::Units::kEarthEquatorialRadius * GPP::Units::kEarthEquatorialRadius);
    }

    // The first black hole with a transform: the one the integrated tracer and the camera observer refer to.
    [[nodiscard]] inline std::optional<std::pair<std::uint64_t, HoleFrame>> PrimaryHole(const GPP::Scene& scene)
    {
        for (auto [entity, hole, transform] : scene.Registry().view<const BlackHoleComponent, const GPP::TransformComponent>().each())
        {
            return std::pair{scene.GuidOf(entity), MakeHoleFrame(hole, transform.Position)};
        }
        return std::nullopt;
    }

    // Velocity relative to the primary hole's ZAMO of the Geodesic Body with this guid, while it is in free fall
    // around that hole; nullopt otherwise (no such body, captured, or following another hole).
    [[nodiscard]] inline std::optional<Vector3> BodyObserverVelocity(const GPP::Scene& scene, const std::uint64_t guid)
    {
        if (guid == 0) return std::nullopt;
        const auto entity = scene.FindByGuid(guid);
        if (!scene.IsValid(entity)) return std::nullopt;
        const auto* body = scene.Registry().try_get<const GeodesicBodyComponent>(entity);
        const auto primary = PrimaryHole(scene);
        if (!body || !primary || body->Status != GeodesicBodyStatus::FreeFall) return std::nullopt;
        if (body->BlackHole != 0 && body->BlackHole != primary->first) return std::nullopt;
        return ZamoVelocity(primary->second.Metric, body->State);
    }

    [[nodiscard]] inline std::string_view ObserverName(const ObserverKind kind)
    {
        switch (kind)
        {
        case ObserverKind::Zamo: return "ZAMO";
        case ObserverKind::Static: return "Static";
        case ObserverKind::Orbiting: return "Orbiting";
        case ObserverKind::Raindrop: return "Raindrop";
        }
        return "";
    }

    // `metricType` is the render setting spacetime.solution; preset observers use it like the renderer does.
    // Geodesic bodies always use their hole's full metric, as the simulation does.
    [[nodiscard]] inline ObserverReadout ComputeObserverReadout(const GPP::Scene& scene, const std::int32_t metricType,
                                                                const ObserverKind kind, const OrbitDirection direction,
                                                                const glm::dvec3& cameraPositionM,
                                                                const std::uint64_t observerGuid)
    {
        ObserverReadout readout;
        const auto primary = PrimaryHole(scene);
        if (!primary)
        {
            readout.Status = "No black hole";
            return readout;
        }
        const HoleFrame& hole = primary->second;

        const auto entity = observerGuid != 0 ? scene.FindByGuid(observerGuid) : decltype(scene.FindByGuid(0)){};
        const auto* body = observerGuid != 0 && scene.IsValid(entity)
                               ? scene.Registry().try_get<const GeodesicBodyComponent>(entity)
                               : nullptr;
        if (body)
        {
            readout.Source = ObserverSource::Body;
            readout.Radius = body->State.R;
            readout.ProperTimeS = body->ProperTime;
            readout.ClockRate = body->ClockRate;
            readout.Speed = body->Speed;
            readout.Gamma = body->Speed < 1.0 ? 1.0 / std::sqrt(1.0 - body->Speed * body->Speed) : 0.0;
            readout.FreeFall = body->Status == GeodesicBodyStatus::FreeFall;
            readout.InsideHorizon = body->Status == GeodesicBodyStatus::Captured;
            if (!BodyObserverVelocity(scene, observerGuid))
            {
                readout.Source = ObserverSource::Fallback;
                readout.Status = body->Status == GeodesicBodyStatus::FreeFall
                                     ? "Follows another black hole; the view uses the ZAMO"
                                     : "Not in free fall; the view uses the ZAMO";
            }
            else
            {
                readout.Status = "Geodesic body in free fall";
            }
            return readout;
        }

        // Preset observer of the render setting, in the metric the renderer draws
        const auto& metricSource = scene.Registry().get<const BlackHoleComponent>(scene.FindByGuid(primary->first));
        const auto physical = BlackHoleMetric(metricSource);
        const auto metric = MakeKerrNewmanParams(metricType, 1.0, physical.SpinParameter, physical.Charge,
                                                 kExtremalHorizonParameter);
        HoleFrame view = hole;
        view.Metric = metric;
        const auto bl = WorldToBoyerLindquist(view, cameraPositionM);
        readout.Radius = bl.x;
        if (!(bl.x > OuterHorizonRadius(metric)))
        {
            readout.InsideHorizon = true;
            readout.Status = "Inside the horizon";
            return readout;
        }

        readout.Source = ObserverSource::Preset;
        readout.Kind = kind;
        auto velocity = ObserverVelocity(metric, kind, bl.x, bl.y, direction);
        if (!velocity)
        {
            readout.Source = ObserverSource::Fallback;
            readout.Kind = ObserverKind::Zamo;
            readout.Status = std::format("{} observer cannot exist here; the view uses the ZAMO", ObserverName(kind));
            velocity = Vector3{0.0, 0.0, 0.0};
        }
        else
        {
            readout.Status = std::format("{} observer", ObserverName(kind));
        }
        readout.Speed = std::sqrt(Dot3(*velocity, *velocity));
        readout.Gamma = LorentzFactor(*velocity);
        readout.ClockRate = ProperTimeRate(metric, bl.x, bl.y, *velocity);
        readout.FreeFall = readout.Kind == ObserverKind::Raindrop
                           || (readout.Kind == ObserverKind::Orbiting && std::abs(std::cos(bl.y)) < 1e-9);
        const auto acceleration = ProperAcceleration(metric, readout.Kind, bl.x, bl.y, direction);
        readout.AccelerationMs2 = acceleration.value_or(0.0) * GPP::Units::kSpeedOfLight * GPP::Units::kSpeedOfLight / hole.LengthM;
        return readout;
    }
}
