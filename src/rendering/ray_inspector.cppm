export module MoleHole:Rendering.RayInspector;

import std;
import glm;
import GPP;
import :Simulation.Components;
import :Simulation.GeodesicBodies;
import :Simulation.ObserverReadout;
import :Rendering.Metric;
import :Rendering.Settings;
import :Rendering.Data;
import :Rendering.Camera;
import :Rendering.OrbitOverlays;
import :Physics.Geodesic;
import :Physics.Observer;

// The ray inspector: the camera ray of one pixel, traced on the CPU the way the Integrated tracer traces it
// (rk4RayMarching in shaders/ray_tracing.glsl): straight to the first hole's integration sphere of 50 M, then backwards
// along the null geodesic of the render metric from the local ZAMO, then straight to the sky. The path is drawn as an
// overlay and its numbers (impact parameter, closest approach, windings, disk crossings and their redshift) are shown
// next to it. The integrator is the adaptive Dormand–Prince one of src/physics/geodesic.cppm, so the path is the exact
// light ray the shader approximates. Background: docs/USER_GUIDE.md, "Ray inspector".
export namespace MoleHole
{
    enum class RayFate : std::int32_t { Sky = 0, Horizon = 1, Object = 2, StepLimit = 3 };

    [[nodiscard]] constexpr std::string_view RayFateName(const RayFate fate) noexcept
    {
        switch (fate)
        {
        case RayFate::Sky: return "escaped to the sky";
        case RayFate::Horizon: return "fell into the black hole";
        case RayFate::Object: return "hit an object";
        case RayFate::StepLimit: return "ran out of steps";
        }
        return "?";
    }

    // Beyond this radius (in M) the tracer lets rays travel straight; INTEGRATION_RADIUS_M in shaders/ray_tracing.glsl
    inline constexpr double kInspectorIntegrationRadius = 50.0;

    struct RayDiskCrossing
    {
        glm::dvec3 PointM{0.0};
        double RadiusGeo{0.0}; // Boyer–Lindquist r in M
        double G{1.0};         // nu_camera / nu_emitted for gas on a prograde circular orbit
        double GGravity{1.0};  // its gravitational part, the ratio of the ZAMO lapses
        int Order{0};          // equator crossings so far, this one included (the image order of the debug view)
    };

    struct RayInspection
    {
        bool Valid = false;
        bool Curved = false; // traced through a black hole's metric; false: a straight line
        std::vector<glm::dvec3> PathM;
        std::size_t CurvedBegin = 0;     // PathM[CurvedBegin .. CurvedEnd) is the integrated part
        std::size_t CurvedEnd = 0;
        glm::dvec3 StartDirection{0.0};  // the pixel's direction at the camera
        glm::dvec3 EscapeDirection{0.0}; // where the light came from, for rays that reach the sky
        RayFate Fate = RayFate::Sky;
        std::string ObjectName;
        std::uint64_t ObjectGuid = 0;

        std::string HoleName;
        double LengthPerGeoM = 1.0; // metres per M of the hole
        double ImpactParameterGeo = std::numeric_limits<double>::quiet_NaN(); // b = L / E
        double CarterGeo2 = std::numeric_limits<double>::quiet_NaN();         // Q / E²
        double ClosestRadiusGeo = std::numeric_limits<double>::quiet_NaN();
        double PhotonOrbitProgradeGeo = std::numeric_limits<double>::quiet_NaN();
        double PhotonOrbitRetrogradeGeo = std::numeric_limits<double>::quiet_NaN();
        double HorizonGeo = std::numeric_limits<double>::quiet_NaN();
        double Windings = 0.0; // |Δφ| / 2π inside the integration sphere
        int EquatorCrossings = 0;
        double DeflectionDeg = 0.0;
        std::int64_t Steps = 0;
        std::vector<RayDiskCrossing> DiskCrossings;
        std::vector<std::string> Notes;
    };

    // World direction of the camera ray through normalised device coordinates (x right, y up, both -1..1), as
    // cameraRay in shaders/trace_common.glsl.
    [[nodiscard]] inline glm::dvec3 CameraPixelDirection(const Camera& camera, const double aspect, const glm::dvec2 ndc)
    {
        const double tanHalfFov = std::tan(glm::radians(static_cast<double>(camera.GetFov())) * 0.5);
        return glm::normalize(glm::dvec3(camera.GetFront()) + glm::dvec3(camera.GetRight()) * (ndc.x * aspect * tanHalfFov)
                              + glm::dvec3(camera.GetUp()) * (ndc.y * tanHalfFov));
    }

    namespace RayInspectorDetail
    {
        struct SphereHit
        {
            double T = std::numeric_limits<double>::infinity();
            std::uint64_t Guid = 0;
            std::string Name;
        };

        [[nodiscard]] inline std::string EntityName(const GPP::Scene& scene, const entt::entity entity)
        {
            if (const auto* metadata = scene.Registry().try_get<const GPP::MetadataComponent>(entity)) return metadata->Name;
            return "object";
        }

        // Closest sphere or star along origin + t dir, 0 < t < maxT; the bodies the tracer draws (FillSceneData)
        [[nodiscard]] inline SphereHit FirstSphere(const GPP::Scene& scene, const glm::dvec3& origin, const glm::dvec3& dir,
                                                   const double maxT)
        {
            SphereHit best;
            best.T = maxT;
            const auto test = [&](const entt::entity entity, const glm::dvec3& center, const double radius)
            {
                if (IsHidden(scene, entity)) return;
                const glm::dvec3 oc = origin - center;
                const double b = glm::dot(oc, dir);
                const double c = glm::dot(oc, oc) - radius * radius;
                const double disc = b * b - c;
                if (disc < 0.0) return;
                const double root = std::sqrt(disc);
                double t = -b - root;
                if (t <= 0.0) t = -b + root;
                if (t <= 0.0 || t >= best.T) return;
                best.T = t;
                best.Guid = scene.GuidOf(entity);
                best.Name = EntityName(scene, entity);
            };
            const auto& registry = scene.Registry();
            for (auto [entity, star, transform] : registry.view<const StarComponent, const GPP::TransformComponent>().each())
                test(entity, transform.Position, star.Radius);
            for (auto [entity, sphere, transform] : registry.view<const SphereComponent, const GPP::TransformComponent>().each())
            {
                if (registry.all_of<StarComponent>(entity)) continue;
                test(entity, transform.Position, sphere.Radius);
            }
            if (!std::isfinite(best.T) || best.T >= maxT) best.Guid = 0;
            return best;
        }

        // Straight ray without a hole: the Straight transport, or a scene without black holes
        inline void TraceStraight(const GPP::Scene& scene, const glm::dvec3& origin, const glm::dvec3& dir,
                                  const double length, RayInspection& out)
        {
            out.PathM.push_back(origin);
            const auto hit = FirstSphere(scene, origin, dir, length);
            double end = length;
            if (hit.Guid != 0)
            {
                end = hit.T;
                out.Fate = RayFate::Object;
                out.ObjectGuid = hit.Guid;
                out.ObjectName = hit.Name;
            }
            for (auto [entity, hole, transform] : scene.Registry().view<const BlackHoleComponent, const GPP::TransformComponent>().each())
            {
                const double radius = GPP::Units::GravitationalRadius(hole.Mass) * OuterHorizonRadius(BlackHoleMetric(hole));
                const glm::dvec3 oc = origin - transform.Position;
                const double b = glm::dot(oc, dir);
                const double disc = b * b - (glm::dot(oc, oc) - radius * radius);
                if (disc < 0.0) continue;
                const double t = -b - std::sqrt(disc);
                if (t > 0.0 && t < end)
                {
                    end = t;
                    out.Fate = RayFate::Horizon;
                    out.ObjectGuid = 0;
                    out.ObjectName.clear();
                }
            }
            out.PathM.push_back(origin + dir * end);
            out.EscapeDirection = dir;
        }
    }

    // Traces the camera ray leaving `originM` along `direction` (world, unit). `observerGuid` is the possessed camera
    // body, as RenderInputs::ObserverGuid. Uses the settings' transport, solution, observer and disk toggle.
    [[nodiscard]] inline RayInspection InspectRay(const GPP::Scene& scene, const glm::dvec3& originM, const glm::dvec3& direction,
                                                  const RenderSettings& settings, const std::uint64_t observerGuid = 0,
                                                  const std::int64_t maxSteps = 200000)
    {
        using namespace RayInspectorDetail;
        RayInspection out;
        out.Valid = true;
        const glm::dvec3 dir = glm::normalize(direction);
        out.StartDirection = dir;

        const auto primary = PrimaryHole(scene);
        const double sceneScale = std::max(glm::length(originM), 1.0);
        if (!primary || settings.TransportMode == Transport::Straight)
        {
            if (primary) out.Notes.emplace_back("Straight transport: rays are not bent");
            TraceStraight(scene, originM, dir, 4.0 * sceneScale, out);
            out.CurvedBegin = out.CurvedEnd = 0;
            return out;
        }
        if (settings.TransportMode == Transport::AnalyticLens)
            out.Notes.emplace_back("Exact geodesic; the Analytic lens image approximates it");
        if (settings.ThirdPerson) out.Notes.emplace_back("Third-person camera: traced from the camera itself");

        // The render metric of the first hole, as the tracer and the observer readout use it
        const auto holeEntity = scene.FindByGuid(primary->first);
        const auto& component = scene.Registry().get<const BlackHoleComponent>(holeEntity);
        const auto physical = BlackHoleMetric(component);
        HoleFrame hole = primary->second;
        hole.Metric = MakeKerrNewmanParams(static_cast<std::int32_t>(settings.Solution), 1.0, physical.SpinParameter, physical.Charge);
        const KerrNewmanParams& metric = hole.Metric;
        out.Curved = true;
        out.HoleName = EntityName(scene, holeEntity);
        out.LengthPerGeoM = hole.LengthM;
        out.HorizonGeo = OuterHorizonRadius(metric);
        out.PhotonOrbitProgradeGeo = PhotonOrbitRadius(metric, OrbitDirection::Prograde);
        out.PhotonOrbitRetrogradeGeo = PhotonOrbitRadius(metric, OrbitDirection::Retrograde);

        // A camera that is not the ZAMO sees the sky aberrated: turn its pixel direction into the ZAMO's
        glm::dvec3 zamoDir = dir;
        double observerShift = 1.0;
        const auto cameraBL = WorldToBoyerLindquist(hole, originM);
        if (const auto observer = FindCameraObserver(scene, static_cast<std::int32_t>(settings.Solution),
                                                     static_cast<ObserverKind>(settings.Observer),
                                                     static_cast<OrbitDirection>(settings.ObserverOrbit), originM, observerGuid))
        {
            const auto [er, eTheta, ePhi] = BoyerLindquistBasisWorld(hole, cameraBL);
            const Vector3 k{-glm::dot(dir, er), -glm::dot(dir, eTheta), -glm::dot(dir, ePhi)};
            const auto boosted = BoostPhoton(observer->Velocity, k);
            observerShift = boosted.EnergyRatio;
            zamoDir = glm::normalize(-(boosted.Direction[0] * er + boosted.Direction[1] * eTheta + boosted.Direction[2] * ePhi));
            out.Notes.emplace_back(std::format("Moving camera ({}): direction aberrated into the ZAMO frame",
                                               ObserverName(static_cast<ObserverKind>(settings.Observer))));
        }

        out.PathM.push_back(originM);
        if (!(cameraBL.x > out.HorizonGeo))
        {
            out.Fate = RayFate::Horizon;
            out.Notes.emplace_back("The camera is inside the horizon");
            return out;
        }

        // Straight part outside the integration sphere
        const double influenceM = kInspectorIntegrationRadius * hole.LengthM;
        glm::dvec3 marchOrigin = originM;
        const glm::dvec3 rel = originM - hole.Position;
        if (glm::dot(rel, rel) > influenceM * influenceM)
        {
            const double enter = influenceM * 0.999;
            const double b = glm::dot(rel, zamoDir);
            const double disc = b * b - (glm::dot(rel, rel) - enter * enter);
            const double reach = b >= 0.0 || disc < 0.0 ? 4.0 * std::max(glm::length(rel), influenceM) : -b - std::sqrt(disc);
            const auto hit = FirstSphere(scene, originM, zamoDir, reach);
            if (hit.Guid != 0)
            {
                out.PathM.push_back(originM + zamoDir * hit.T);
                out.Fate = RayFate::Object;
                out.ObjectGuid = hit.Guid;
                out.ObjectName = hit.Name;
                return out;
            }
            if (b >= 0.0 || disc < 0.0)
            {
                out.PathM.push_back(originM + zamoDir * reach);
                out.EscapeDirection = zamoDir;
                out.DeflectionDeg = glm::degrees(std::acos(std::clamp(glm::dot(dir, zamoDir), -1.0, 1.0)));
                out.Notes.emplace_back("Misses the integration sphere of 50 M: no bending to trace");
                return out;
            }
            marchOrigin = originM + zamoDir * reach;
            out.PathM.push_back(marchOrigin);
        }

        // The photon arriving at the camera travels along -dir: launch it from the local ZAMO, integrate backwards
        const auto startBL = WorldToBoyerLindquist(hole, marchOrigin);
        const auto [er, eTheta, ePhi] = BoyerLindquistBasisWorld(hole, startBL);
        const std::array<double, 3> n{-glm::dot(zamoDir, er), -glm::dot(zamoDir, eTheta), -glm::dot(zamoDir, ePhi)};
        auto photon = MakePhotonFromZamoDirection<double>(metric, startBL.x, startBL.y, startBL.z, n);
        if (!photon)
        {
            out.Fate = RayFate::Horizon;
            return out;
        }
        out.CurvedBegin = out.PathM.size() - 1;
        const double energy = photon->Energy;
        const double angularMomentum = photon->AngularMomentum;
        out.ImpactParameterGeo = angularMomentum / energy;
        out.CarterGeo2 = CarterConstant(metric, *photon) / (energy * energy);

        const auto cameraZamo = EvaluateZamoFrame(metric, cameraBL.x, cameraBL.y);
        const double cameraEnergy = (energy - cameraZamo.Omega * angularMomentum) / std::max(cameraZamo.Alpha, 1e-12) / observerShift;
        const double diskInner = ProgradeIscoRadius(metric);
        const double diskOuter = kDiskOuterRadiusM * metric.Mass;
        const auto diskG = [&](const double r) -> std::pair<double, double>
        {
            const auto emitter = EvaluateZamoFrame(metric, r, std::numbers::pi / 2.0);
            double omega = OrbitalFrequency(metric, r, OrbitDirection::Prograde);
            double norm = CircularOrbitNorm(metric, r, OrbitDirection::Prograde);
            if (!(norm > 1e-6) || !std::isfinite(omega))
            {
                omega = emitter.Omega;
                const auto g = EvaluateEquatorialMetric(metric, r);
                norm = -(g.Gtt + 2.0 * g.Gtphi * omega + g.Gphiphi * omega * omega);
            }
            const double ut = 1.0 / std::sqrt(std::max(norm, 1e-12));
            return {cameraEnergy / (ut * (energy - omega * angularMomentum)), emitter.Alpha / std::max(cameraZamo.Alpha, 1e-12)};
        };

        const double capture = CaptureRadius(metric);
        double closest = startBL.x;
        double windings = 0.0;
        GeodesicVector<double> previous = ToVector(*photon);
        std::int64_t steps = 0;
        bool hitObject = false;
        GeodesicTolerance tolerance;
        tolerance.Relative = 1e-9;
        tolerance.Absolute = 1e-11;
        tolerance.MaxSteps = maxSteps;
        const auto toWorld = [&](const GeodesicVector<double>& y) { return BoyerLindquistToWorld(hole, y[0], y[1], y[4]); };

        IntegrateGeodesic(metric, *photon, tolerance,
                          [&](const GeodesicVector<double>& y, const GeodesicVector<double>& dy, const double) -> std::optional<double>
                          {
                              if (y != previous)
                              {
                                  ++steps;
                                  const glm::dvec3 from = toWorld(previous);
                                  const glm::dvec3 to = toWorld(y);
                                  closest = std::min(closest, y[0]);
                                  windings += std::abs(y[4] - previous[4]);

                                  const glm::dvec3 chord = to - from;
                                  const double chordLength = glm::length(chord);
                                  if (chordLength > 0.0)
                                  {
                                      const auto hit = FirstSphere(scene, from, chord / chordLength, chordLength);
                                      if (hit.Guid != 0)
                                      {
                                          out.PathM.push_back(from + chord / chordLength * hit.T);
                                          out.Fate = RayFate::Object;
                                          out.ObjectGuid = hit.Guid;
                                          out.ObjectName = hit.Name;
                                          hitObject = true;
                                          return std::nullopt;
                                      }
                                  }

                                  const double c0 = std::cos(previous[1]);
                                  const double c1 = std::cos(y[1]);
                                  if ((c0 < 0.0) != (c1 < 0.0))
                                  {
                                      ++out.EquatorCrossings;
                                      const double f = c0 / (c0 - c1);
                                      const double rCross = previous[0] + f * (y[0] - previous[0]);
                                      const double phiCross = previous[4] + f * (y[4] - previous[4]);
                                      if (settings.AccretionDisk && rCross >= diskInner && rCross <= diskOuter)
                                      {
                                          const auto [g, gGravity] = diskG(rCross);
                                          out.DiskCrossings.push_back({BoyerLindquistToWorld(hole, rCross, std::numbers::pi / 2.0, phiCross),
                                                                       rCross, g, gGravity, out.EquatorCrossings});
                                      }
                                  }
                                  out.PathM.push_back(to);
                                  previous = y;
                              }
                              if (y[0] > kInspectorIntegrationRadius * metric.Mass) return std::nullopt;
                              // Keep the polyline smooth: a few percent of the distance to the horizon per step, and a
                              // bounded sweep in angle (dθ/dλ, dφ/dλ), backwards in the affine parameter
                              const double radial = 0.03 * std::max(y[0] - capture, 1e-6 * metric.Mass);
                              const double angular = 0.02 / std::max(std::abs(dy[1]) + std::abs(dy[4]), 1e-12);
                              const double speed = std::max(std::abs(dy[0]), 1e-12);
                              return -std::min(radial / speed, angular);
                          });
        out.Steps = steps;
        out.CurvedEnd = out.PathM.size();
        out.ClosestRadiusGeo = closest;
        out.Windings = windings / (2.0 * std::numbers::pi);
        if (hitObject) return out;
        if (photon->Status == GeodesicStatus::Captured || photon->Status == GeodesicStatus::NonFinite)
        {
            out.Fate = RayFate::Horizon;
            return out;
        }
        if (photon->Status == GeodesicStatus::StepLimit)
        {
            out.Fate = RayFate::StepLimit;
            return out;
        }

        // Escaped: on along the last tangent. The integration ran backwards, so the light came from where the path heads.
        const auto y = ToVector(*photon);
        const auto dy = GeodesicDerivative(MakeSpacetime<double>(metric), energy, angularMomentum, y);
        GeodesicVector<double> ahead = y;
        const double h = -1e-4 * std::max(y[0], 1.0) / std::max(std::abs(dy[0]), 1e-12);
        for (std::size_t i = 0; i < ahead.size(); ++i) ahead[i] += h * dy[i];
        const glm::dvec3 exit = toWorld(y);
        out.EscapeDirection = glm::normalize(toWorld(ahead) - exit);
        out.DeflectionDeg = glm::degrees(std::acos(std::clamp(glm::dot(dir, out.EscapeDirection), -1.0, 1.0)));
        const double tail = std::max(glm::distance(originM, hole.Position), influenceM);
        const auto hit = FirstSphere(scene, exit, out.EscapeDirection, 4.0 * tail);
        if (hit.Guid != 0)
        {
            out.PathM.push_back(exit + out.EscapeDirection * hit.T);
            out.Fate = RayFate::Object;
            out.ObjectGuid = hit.Guid;
            out.ObjectName = hit.Name;
            return out;
        }
        out.PathM.push_back(exit + out.EscapeDirection * tail);
        out.Fate = RayFate::Sky;
        return out;
    }

    inline constexpr glm::vec3 kInspectorStraightColor{0.75f, 0.75f, 0.75f};
    inline constexpr glm::vec3 kInspectorNearColor{1.0f, 0.95f, 0.35f};
    inline constexpr glm::vec3 kInspectorFarColor{1.0f, 0.3f, 0.1f};
    inline constexpr glm::vec3 kInspectorDiskColor{0.3f, 0.9f, 1.0f};

    // The path as overlay lines: straight parts grey, the integrated part from yellow (at the camera) to red, and a
    // cyan cross where it crosses the accretion disk
    [[nodiscard]] inline std::vector<OverlaySegment> RayInspectionOverlay(const RayInspection& ray)
    {
        std::vector<OverlaySegment> out;
        if (!ray.Valid || ray.PathM.size() < 2) return out;
        const std::size_t curvedSegments = ray.CurvedEnd > ray.CurvedBegin + 1 ? ray.CurvedEnd - ray.CurvedBegin - 1 : 0;
        for (std::size_t i = 1; i < ray.PathM.size(); ++i)
        {
            glm::vec3 color = kInspectorStraightColor;
            if (i > ray.CurvedBegin && i < ray.CurvedEnd && curvedSegments > 0)
            {
                const float t = static_cast<float>(i - ray.CurvedBegin - 1) / static_cast<float>(curvedSegments);
                color = glm::mix(kInspectorNearColor, kInspectorFarColor, t);
            }
            out.push_back({ray.PathM[i - 1], ray.PathM[i], color});
        }
        const double size = 0.4 * ray.LengthPerGeoM;
        for (const auto& crossing : ray.DiskCrossings)
        {
            for (const glm::dvec3 axis : {glm::dvec3(1, 0, 0), glm::dvec3(0, 1, 0), glm::dvec3(0, 0, 1)})
                out.push_back({crossing.PointM - axis * size, crossing.PointM + axis * size, kInspectorDiskColor});
        }
        return out;
    }
}
