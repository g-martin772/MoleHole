module;
#include <glm/glm.hpp>
export module MoleHole:Simulation.TidalReadout;

import std;
import GPP;
import :Simulation.Components;
import :Simulation.GeodesicBodies;
import :Rendering.Metric;
import :Physics.Geodesic;
import :Physics.Observer;
import :Physics.Tidal;

// The tidal tensor (:Physics.Tidal) of observers and Geodesic Bodies in the scene, in SI and with its principal axes
// as world directions: what the camera window, the HUD, the tidal overlay and the Tidal Stretch node show.
// Background: docs/notes/tidal_forces.md
export namespace MoleHole
{
    struct TidalSample
    {
        Vector3 EigenvaluesS2{};               // s⁻², ascending: [0] strongest stretch (negative), [2] strongest squeeze
        std::array<glm::dvec3, 3> AxesWorld{}; // unit world direction of each eigenvalue's axis
    };

    // Tidal tensor of an observer moving with `velocity` relative to the ZAMO at Boyer–Lindquist `bl`, in the metric of
    // `hole`. The observer's axes are its boosted ZAMO axes; they are placed in the world along the ZAMO's directions
    // (BoyerLindquistBasisWorld), as the renderer places the camera's.
    [[nodiscard]] inline TidalSample MakeTidalSample(const HoleFrame& hole, const glm::dvec3& bl, const Vector3& velocity)
    {
        const auto tensor = TidalTensorSi(TidalTensor(hole.Metric, bl.x, bl.y, velocity), hole.LengthM, GPP::Units::kSpeedOfLight);
        const auto axes = PrincipalAxes(tensor);
        const auto basis = BoyerLindquistBasisWorld(hole, bl);
        TidalSample sample;
        sample.EigenvaluesS2 = axes.Eigenvalues;
        for (std::size_t k = 0; k < 3; ++k)
        {
            const auto& v = axes.Axes[k];
            sample.AxesWorld[k] = glm::normalize(v[0] * basis[0] + v[1] * basis[1] + v[2] * basis[2]);
        }
        return sample;
    }

    // What a Geodesic Body in free fall feels, in its hole's full metric; nullopt for a body that is not falling freely
    // (not launched, captured, no hole).
    [[nodiscard]] inline std::optional<TidalSample> BodyTidalSample(const GPP::Scene& scene, const entt::entity entity)
    {
        const auto* body = scene.Registry().try_get<const GeodesicBodyComponent>(entity);
        if (!body || body->Status != GeodesicBodyStatus::FreeFall) return std::nullopt;
        const auto hole = FindHoleFrame(scene, body->BlackHole);
        if (!hole) return std::nullopt;
        return MakeTidalSample(*hole, {body->State.R, body->State.Theta, body->State.Phi}, ZamoVelocity(hole->Metric, body->State));
    }

    // Difference in acceleration across `lengthM` along the strongest stretch axis, in m/s²: what pulls a body of that
    // size apart (head to foot for a person falling feet first).
    [[nodiscard]] inline double TidalStretchAcrossMs2(const TidalSample& sample, const double lengthM)
    {
        return -std::min(sample.EigenvaluesS2[0], 0.0) * lengthM;
    }
}
