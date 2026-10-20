module;
#include <glm/glm.hpp>
export module MoleHole:Simulation.OrbitTools;

import std;
import GPP;
import :Simulation.Components;
import :Simulation.GeodesicBodies;
import :Simulation.ObserverReadout;
import :Rendering.Metric;
import :Physics.Observer;

// Relativity helpers behind the animation-graph nodes "Clock Rate" and "Launch Circular Orbit". The interpreter
// (animation_graph_executor.cpp) and the Luau host (script_runtime.cpp) both call these, so both runtimes produce the
// same numbers, writes and diagnostics. Background: docs/notes/orbit_overlays_and_nodes.md
export namespace MoleHole
{
    // dτ/dt of a preset observer at a world position around the first black hole, in its full metric (as the
    // simulation uses it). 0 where that observer cannot exist there: inside the horizon, static inside the
    // ergosphere, orbiting inside the photon orbit. `observer` is ObserverKind as an int (0 ZAMO, 1 Static,
    // 2 Orbiting, 3 Raindrop); anything else counts as the ZAMO.
    [[nodiscard]] inline double ObserverClockRateAt(const GPP::Scene& scene, const glm::dvec3& positionM, const int observer)
    {
        const auto primary = PrimaryHole(scene);
        if (!primary) return 1.0; // flat space without a black hole
        const HoleFrame& hole = primary->second;
        const auto kind = observer >= 0 && observer <= 3 ? static_cast<ObserverKind>(observer) : ObserverKind::Zamo;
        const auto bl = WorldToBoyerLindquist(hole, positionM);
        const auto velocity = ObserverVelocity(hole.Metric, kind, bl.x, bl.y);
        if (!velocity) return 0.0;
        return ProperTimeRate(hole.Metric, bl.x, bl.y, *velocity);
    }

    // The writes that put a Geodesic Body on a circular equatorial orbit of Boyer–Lindquist radius `radius` (in M of
    // its hole) at azimuth `azimuth` (rad, about the spin axis, from the frame's X axis): its position, Launch =
    // Orbiting and Retrograde. The simulation re-launches the body on its next tick because it was moved. Returns a
    // diagnostic instead when that is impossible.
    [[nodiscard]] inline std::expected<std::vector<GPP::SetFieldCommand>, std::string>
    LaunchCircularOrbitWrites(const GPP::Scene& scene, const std::uint64_t guid, const double radius, const double azimuth,
                              const bool retrograde)
    {
        const auto entity = scene.FindByGuid(guid);
        if (guid == 0 || !scene.IsValid(entity)) return std::unexpected(std::string("Entity input is empty or not found"));
        const auto* body = scene.Registry().try_get<GeodesicBodyComponent>(entity);
        if (!body) return std::unexpected(std::string("Entity has no Geodesic Body"));
        const auto hole = FindHoleFrame(scene, body->BlackHole);
        if (!hole) return std::unexpected(std::string("The body's black hole is missing"));
        const auto direction = retrograde ? OrbitDirection::Retrograde : OrbitDirection::Prograde;
        if (!(CircularOrbitNorm(hole->Metric, radius, direction) > 0.0))
        {
            return std::unexpected(std::format("No circular orbit at r = {:.4g} M (inside the photon orbit)", radius));
        }
        const glm::dvec3 position = BoyerLindquistToWorld(*hole, radius, std::numbers::pi / 2.0, azimuth);
        return std::vector<GPP::SetFieldCommand>{
            GPP::SetFieldCommand{guid, "Transform", "Position", position},
            GPP::SetFieldCommand{guid, "GeodesicBody", "Launch", static_cast<int>(GeodesicLaunch::Orbiting)},
            GPP::SetFieldCommand{guid, "GeodesicBody", "Retrograde", retrograde},
        };
    }
}
