module;
#include <glm/glm.hpp>
export module MoleHole:Rendering.OrbitOverlays;

import std;
import GPP;
import :Simulation.Components;
import :Simulation.GeodesicBodies;
import :Rendering.Metric;
import :Physics.Geodesic;
import :Physics.Observer;
import :Simulation.ObserverReadout;

// Line geometry of the orbit overlays, in world metres: the characteristic radii of every black hole and the
// predicted world lines of the geodesic bodies. Computed from a scene snapshot (never the simulation thread's scene)
// and drawn by OrbitOverlayStage in render_pipeline.cpp. Like the other overlays these lines are rasterised, not
// lensed. Background: docs/notes/orbit_overlays_and_nodes.md
export namespace MoleHole
{
    struct OverlaySegment
    {
        glm::dvec3 A{0.0};
        glm::dvec3 B{0.0};
        glm::vec3 Color{1.0f};
    };

    // Colours of the characteristic radii; retrograde orbits use the same hue at half brightness.
    inline constexpr glm::vec3 kHorizonOverlayColor{0.85f, 0.85f, 0.85f};
    inline constexpr glm::vec3 kErgosphereOverlayColor{0.65f, 0.35f, 1.0f};
    inline constexpr glm::vec3 kPhotonOrbitOverlayColor{1.0f, 0.6f, 0.15f};
    inline constexpr glm::vec3 kMarginallyBoundOverlayColor{0.35f, 0.9f, 0.4f};
    inline constexpr glm::vec3 kIscoOverlayColor{0.2f, 0.75f, 1.0f};
    inline constexpr float kRetrogradeOverlayDimming = 0.5f;

    // A curve through BL (r(θ), θ, φ = phi) from pole to pole, or the equatorial circle of radius r when `meridian` is
    // false; both in the hole's spin frame, as the renderer and the geodesic module place them.
    inline void AppendBoyerLindquistCurve(std::vector<OverlaySegment>& out, const HoleFrame& hole,
                                          const std::function<double(double)>& radius, const bool meridian,
                                          const double phi, const glm::vec3& color, const int segments)
    {
        const auto point = [&](const int k)
        {
            const double t = static_cast<double>(k) / segments;
            if (meridian) return BoyerLindquistToWorld(hole, radius(std::numbers::pi * t), std::numbers::pi * t, phi);
            return BoyerLindquistToWorld(hole, radius(std::numbers::pi / 2.0), std::numbers::pi / 2.0, 2.0 * std::numbers::pi * t);
        };
        glm::dvec3 previous = point(0);
        for (int k = 1; k <= segments; ++k)
        {
            const glm::dvec3 next = point(k);
            out.push_back({previous, next, color});
            previous = next;
        }
    }

    // Horizon (equator and two meridians of the oblate horizon surface), ergosphere (its static-limit surface in two
    // meridian planes) and the equatorial rings of the photon orbit, marginally bound orbit and ISCO, prograde and
    // retrograde. Uses each hole's full metric, the one the geodesic bodies follow (BlackHoleMetric).
    // `skipGuid`: a hole the integrated tracer already draws its rings for (lensed), see FillOrbitOverlayData.
    [[nodiscard]] inline std::vector<OverlaySegment> CharacteristicRadiiOverlay(const GPP::Scene& scene, const int segments = 96,
                                                                                const std::uint64_t skipGuid = 0)
    {
        std::vector<OverlaySegment> out;
        for (auto [entity, component, transform] :
             scene.Registry().view<const BlackHoleComponent, const GPP::TransformComponent>().each())
        {
            if (skipGuid != 0 && scene.GuidOf(entity) == skipGuid) continue;
            const HoleFrame hole = MakeHoleFrame(component, transform.Position);
            const KerrNewmanParams& m = hole.Metric;
            const double horizon = OuterHorizonRadius(m);
            const auto constant = [](const double r) { return [r](double) { return r; }; };
            for (const double phi : {0.0, std::numbers::pi / 2.0})
            {
                AppendBoyerLindquistCurve(out, hole, constant(horizon), true, phi, kHorizonOverlayColor, segments / 2);
                AppendBoyerLindquistCurve(out, hole, [&](const double theta) { return ErgosphereRadius(m, theta); }, true, phi,
                                          kErgosphereOverlayColor, segments / 2);
            }
            AppendBoyerLindquistCurve(out, hole, constant(horizon), false, 0.0, kHorizonOverlayColor, segments);
            for (const auto direction : {OrbitDirection::Prograde, OrbitDirection::Retrograde})
            {
                const float dim = direction == OrbitDirection::Prograde ? 1.0f : kRetrogradeOverlayDimming;
                AppendBoyerLindquistCurve(out, hole, constant(PhotonOrbitRadius(m, direction)), false, 0.0,
                                          kPhotonOrbitOverlayColor * dim, segments);
                AppendBoyerLindquistCurve(out, hole, constant(MarginallyBoundRadius(m, direction)), false, 0.0,
                                          kMarginallyBoundOverlayColor * dim, segments);
                AppendBoyerLindquistCurve(out, hole, constant(IscoRadius(m, direction)), false, 0.0, kIscoOverlayColor * dim,
                                          segments);
            }
        }
        return out;
    }

    // Where a world point appears to a moving camera: its true direction from the camera, as the local ZAMO sees it,
    // aberrated into the camera's frame. This is the inverse of the per-pixel mapping in rk4RayMarching
    // (shaders/ray_tracing.glsl), with the same local basis (blBasis in kerr.glsl), so overlays line up with the
    // traced image of a moving observer. Lensing is not applied. The distance to the camera is kept.
    [[nodiscard]] inline glm::dvec3 AberrateForCamera(const CameraObserverFrame& observer, const glm::dvec3& cameraM,
                                                      const glm::dvec3& pointM)
    {
        const glm::dvec3 offset = pointM - cameraM;
        const double distance = glm::length(offset);
        if (!(distance > 0.0)) return pointM;
        const HoleFrame& hole = observer.Hole;
        const auto bl = WorldToBoyerLindquist(hole, cameraM);
        const double a = hole.Metric.SpinParameter;
        const double r = bl.x;
        const double s = std::sin(bl.y);
        const double c = std::cos(bl.y);
        const double rho = std::sqrt(r * r + a * a);
        const glm::dvec3 er = hole.Frame * glm::normalize(glm::dvec3(r / rho * s * std::cos(bl.z), r / rho * s * std::sin(bl.z), c));
        const glm::dvec3 eTheta = hole.Frame * glm::normalize(glm::dvec3(rho * c * std::cos(bl.z), rho * c * std::sin(bl.z), -r * s));
        const glm::dvec3 ePhi = hole.Frame * glm::dvec3(-std::sin(bl.z), std::cos(bl.z), 0.0);

        // Photon from the point travels along -look; seen from the camera, the ZAMO moves with -v
        const glm::dvec3 look = offset / distance;
        const Vector3 k{-glm::dot(look, er), -glm::dot(look, eTheta), -glm::dot(look, ePhi)};
        const auto& v = observer.Velocity;
        const auto seen = BoostPhoton({-v[0], -v[1], -v[2]}, k).Direction;
        const glm::dvec3 apparent = -(seen[0] * er + seen[1] * eTheta + seen[2] * ePhi);
        return cameraM + glm::normalize(apparent) * distance;
    }

    inline constexpr glm::vec3 kPredictionNearColor{1.0f, 0.9f, 0.3f};
    inline constexpr glm::vec3 kPredictionFarColor{0.35f, 0.25f, 0.05f};
    inline constexpr int kMaxPredictedBodies = 32;

    // Where each free-falling Geodesic Body will be over the next `span` of coordinate time (in M of its hole), from
    // the snapshot's state, sampled at `samples` equal steps of t. Fades from bright (now) to dim (end of the span);
    // stops where a body would cross the horizon.
    // `excludeGuid`: the body a riding camera sits on; its own trail would start in the camera's eye.
    [[nodiscard]] inline std::vector<OverlaySegment> PredictedOrbitsOverlay(const GPP::Scene& scene, const double span,
                                                                            const int samples = 240,
                                                                            const std::uint64_t excludeGuid = 0)
    {
        std::vector<OverlaySegment> out;
        if (!(span > 0.0) || samples <= 0) return out;
        const GeodesicTolerance tolerance{.Relative = 1e-9, .Absolute = 1e-11};
        int bodies = 0;
        for (auto [entity, body] : scene.Registry().view<const GeodesicBodyComponent>().each())
        {
            if (body.Status != GeodesicBodyStatus::FreeFall || scene.GuidOf(entity) == excludeGuid) continue;
            if (++bodies > kMaxPredictedBodies) break;
            const auto hole = FindHoleFrame(scene, body.BlackHole);
            if (!hole) continue;
            GeodesicState<double> state = body.State;
            state.Step = 0.0;
            glm::dvec3 previous = BoyerLindquistToWorld(*hole, state.R, state.Theta, state.Phi);
            for (int k = 1; k <= samples; ++k)
            {
                AdvanceToCoordinateTime(hole->Metric, state, span / samples, tolerance);
                if (state.Status == GeodesicStatus::NonFinite || state.Status == GeodesicStatus::StepLimit) break;
                const glm::dvec3 next = BoyerLindquistToWorld(*hole, state.R, state.Theta, state.Phi);
                const float fade = static_cast<float>(k - 1) / static_cast<float>(samples);
                out.push_back({previous, next, glm::mix(kPredictionNearColor, kPredictionFarColor, fade)});
                previous = next;
                if (state.Status == GeodesicStatus::Captured) break;
            }
        }
        return out;
    }

    // ---- Lensed trails: line segments the integrated tracer draws itself (shaders/trails.glsl) ----

    // A segment in render units, the space the tracer and the rasterised lines share.
    struct RenderSegment
    {
        glm::vec3 A{0.0f};
        glm::vec3 B{0.0f};
        glm::vec3 Color{1.0f};
    };

    // std430 layout of TrailSegment in shaders/trails.glsl: endpoints in the first hole's local frame.
    struct TracedTrailSegment
    {
        glm::vec4 A{0.0f};
        glm::vec4 B{0.0f};
        glm::vec4 Color{0.0f};
    };
    static_assert(sizeof(TracedTrailSegment) == 48);

    // Cells per axis of the (sparse) grid over the integration cube: about 0.4 M at the 50 M integration radius. Fine
    // cells keep both the segments per cell and the ray steps per cell small; only occupied cells are stored.
    inline constexpr int kTrailGridCells = 256;
    inline constexpr std::uint32_t kEmptyTrailCell = 0xFFFFFFFFu;
    // Coarse occupancy bits checked first, so ray steps far from every trail skip the hash lookups (64³ bits = 32 KB)
    inline constexpr int kTrailCoarseCells = 64;
    inline constexpr std::size_t kTrailCoarseWords = static_cast<std::size_t>(kTrailCoarseCells) * kTrailCoarseCells * kTrailCoarseCells / 32;
    // In M: inside it the tracer integrates curved rays, outside it treats them as straight. Mirrors
    // INTEGRATION_RADIUS_M in shaders/ray_tracing.glsl.
    inline constexpr float kTracerIntegrationRadius = 50.0f;
    inline constexpr std::size_t kMaxTracedTrailSegments = 16384;
    inline constexpr std::size_t kMaxTrailGridEntries = std::size_t{1} << 18;
    inline constexpr std::size_t kMaxTrailCells = std::size_t{1} << 18; // hash table slots

    // One occupied grid cell in the open-addressing hash table (std430 uvec4 in shaders/trails.glsl): its key
    // (z·n + y)·n + x, or kEmptyTrailCell, and its range in CellSegments.
    struct TrailCell
    {
        std::uint32_t Key{kEmptyTrailCell};
        std::uint32_t Start{0};
        std::uint32_t Count{0};
        std::uint32_t Pad{0};
    };
    static_assert(sizeof(TrailCell) == 16);

    // Trail segments for the tracer and the sparse grid that lets each ray step test only nearby ones: a hash table
    // of the occupied cells (power-of-two size, at most half full) pointing into CellSegments (indices into Segments).
    struct TracedTrails
    {
        std::vector<TracedTrailSegment> Segments;
        std::vector<TrailCell> Cells;
        std::vector<std::uint32_t> CellSegments;
        std::vector<std::uint32_t> Coarse = std::vector<std::uint32_t>(kTrailCoarseWords, 0u); // occupancy bits
        float Radius{0.0f}; // half-size of the grid cube: the tracer's integration radius, in render units
    };

    // Whether any coarse cell overlapping the box [lo, hi] holds a trail; mirrors trailCoarseHit in trails.glsl.
    [[nodiscard]] inline bool CoarseTrailHit(const TracedTrails& trails, const glm::vec3& lo, const glm::vec3& hi)
    {
        constexpr int n = kTrailCoarseCells;
        const float radius = trails.Radius;
        const auto cell = [&](const float v) { return std::clamp(static_cast<int>(std::floor((v + radius) / (2.0f * radius) * n)), 0, n - 1); };
        for (int z = cell(lo.z); z <= cell(hi.z); ++z)
            for (int y = cell(lo.y); y <= cell(hi.y); ++y)
                for (int x = cell(lo.x); x <= cell(hi.x); ++x)
                {
                    const auto bit = static_cast<std::uint32_t>((z * n + y) * n + x);
                    if (trails.Coarse[bit / 32] & (1u << (bit % 32))) return true;
                }
        return false;
    }

    // Hash slot of a cell key; mirrors trailHash in shaders/trails.glsl (Knuth's multiplicative hash).
    [[nodiscard]] constexpr std::uint32_t TrailHash(const std::uint32_t key, const std::uint32_t mask)
    {
        return (key * 2654435761u) & mask;
    }

    // Slot of `key` in the table, or -1 when the cell is empty (linear probing).
    [[nodiscard]] inline int FindTrailCell(const std::vector<TrailCell>& cells, const std::uint32_t key)
    {
        if (cells.empty()) return -1;
        const auto mask = static_cast<std::uint32_t>(cells.size() - 1);
        for (std::uint32_t i = TrailHash(key, mask), probes = 0; probes < cells.size(); i = (i + 1) & mask, ++probes)
        {
            if (cells[i].Key == key) return static_cast<int>(i);
            if (cells[i].Key == kEmptyTrailCell) return -1;
        }
        return -1;
    }

    // Joins runs of consecutive, equally coloured segments whose inner points lie within `tolerance` of the joined
    // line (object paths record a point every tick, far more than the line needs).
    inline void SimplifyTrails(const std::vector<struct RenderSegment>& segments, float tolerance, std::vector<struct RenderSegment>& out);
    [[nodiscard]] inline std::vector<struct RenderSegment> SimplifyTrails(const std::vector<struct RenderSegment>& segments, float tolerance);

    // The hole's spin frame exactly as spinFrame in shaders/kerr.glsl builds it (columns X, Y, Z = spin axis).
    [[nodiscard]] inline glm::mat3 ShaderSpinFrame(const glm::vec3& spinAxis)
    {
        const glm::vec3 z = glm::normalize(spinAxis);
        const glm::vec3 helper = std::abs(z.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 x = glm::normalize(glm::cross(helper, z));
        const glm::vec3 y = glm::cross(z, x);
        return glm::mat3(x, y, z);
    }

    // Splits segments at the tracer's integration sphere around the first hole (`radius` around `holePosition`,
    // render units). Inside it rays are curved, so those parts go to the tracer in the hole's local frame; outside
    // the tracer itself treats rays as straight, so the rest stays rasterised and is appended to `outside`.
    // `pad` widens each segment's grid footprint by the largest line half-width a ray can test it with.
    inline void BuildTracedTrails(TracedTrails& traced, const std::vector<RenderSegment>& segments, const glm::vec3& holePosition,
                                  const glm::vec3& spinAxis, const float radius, const float pad,
                                  std::vector<RenderSegment>& outside)
    {
        traced.Segments.clear();
        traced.CellSegments.clear();
        traced.Radius = radius;
        if (traced.Coarse.size() == kTrailCoarseWords) std::ranges::fill(traced.Coarse, 0u);
        else traced.Coarse.assign(kTrailCoarseWords, 0u);
        const glm::mat3 toLocal = glm::transpose(ShaderSpinFrame(spinAxis));
        const auto inside = [&](const glm::vec3& p) { return glm::dot(p - holePosition, p - holePosition) < radius * radius; };
        // Parameters where the segment a + t (b - a) crosses the sphere
        const auto crossings = [&](const glm::vec3& a, const glm::vec3& b)
        {
            const glm::vec3 d = b - a;
            const glm::vec3 m = a - holePosition;
            const float qa = glm::dot(d, d);
            const float qb = 2.0f * glm::dot(m, d);
            const float qc = glm::dot(m, m) - radius * radius;
            const float disc = qb * qb - 4.0f * qa * qc;
            if (qa <= 0.0f || disc <= 0.0f) return std::pair{2.0f, 2.0f};
            const float root = std::sqrt(disc);
            return std::pair{(-qb - root) / (2.0f * qa), (-qb + root) / (2.0f * qa)};
        };

        for (const auto& segment : segments)
        {
            const auto [t0, t1] = crossings(segment.A, segment.B);
            const float enter = std::clamp(t0, 0.0f, 1.0f);
            const float leave = std::clamp(t1, 0.0f, 1.0f);
            const auto at = [&](const float t) { return glm::mix(segment.A, segment.B, t); };
            const bool hasInside = leave > enter || (inside(segment.A) && inside(segment.B));
            if (!hasInside)
            {
                outside.push_back(segment);
                continue;
            }
            const float from = inside(segment.A) ? 0.0f : enter;
            const float to = inside(segment.B) ? 1.0f : leave;
            if (from > 0.0f) outside.push_back({segment.A, at(from), segment.Color});
            if (to < 1.0f) outside.push_back({at(to), segment.B, segment.Color});
            if (traced.Segments.size() >= kMaxTracedTrailSegments) continue;
            const glm::vec3 a = toLocal * (at(from) - holePosition);
            const glm::vec3 b = toLocal * (at(to) - holePosition);
            traced.Segments.push_back({glm::vec4(a, 0.0f), glm::vec4(b, 0.0f), glm::vec4(segment.Color, 1.0f)});
        }

        // Bucket every traced segment into the cells its padded bounding box overlaps, then hash the occupied cells
        constexpr int n = kTrailGridCells;
        const auto cell = [&](const float v) { return std::clamp(static_cast<int>(std::floor((v + radius) / (2.0f * radius) * n)), 0, n - 1); };
        // Each ray step looks up only the fine cell of its midpoint and is at most one cell long, so every segment
        // is also registered half a cell further out: then anything within reach of the step is in that cell.
        const float finePad = pad + radius / static_cast<float>(n);
        thread_local std::vector<std::pair<std::uint32_t, std::uint32_t>> entries; // (cell key, segment)
        entries.clear();
        std::size_t kept = 0;
        for (; kept < traced.Segments.size(); ++kept)
        {
            const auto& segment = traced.Segments[kept];
            const glm::vec3 lo = glm::min(glm::vec3(segment.A), glm::vec3(segment.B)) - pad;
            const glm::vec3 hi = glm::max(glm::vec3(segment.A), glm::vec3(segment.B)) + pad;
            const glm::vec3 fineLo = lo - (finePad - pad);
            const glm::vec3 fineHi = hi + (finePad - pad);
            const glm::ivec3 c0(cell(fineLo.x), cell(fineLo.y), cell(fineLo.z));
            const glm::ivec3 c1(cell(fineHi.x), cell(fineHi.y), cell(fineHi.z));
            const std::size_t count = static_cast<std::size_t>(c1.x - c0.x + 1) * (c1.y - c0.y + 1) * (c1.z - c0.z + 1);
            if (entries.size() + count > kMaxTrailGridEntries) break;
            {
                constexpr int m = kTrailCoarseCells;
                const auto coarse = [&](const float v) { return std::clamp(static_cast<int>(std::floor((v + radius) / (2.0f * radius) * m)), 0, m - 1); };
                for (int z = coarse(lo.z); z <= coarse(hi.z); ++z)
                    for (int y = coarse(lo.y); y <= coarse(hi.y); ++y)
                        for (int x = coarse(lo.x); x <= coarse(hi.x); ++x)
                        {
                            const auto bit = static_cast<std::uint32_t>((z * m + y) * m + x);
                            traced.Coarse[bit / 32] |= 1u << (bit % 32);
                        }
            }
            for (int z = c0.z; z <= c1.z; ++z)
                for (int y = c0.y; y <= c1.y; ++y)
                    for (int x = c0.x; x <= c1.x; ++x)
                    {
                        entries.emplace_back(static_cast<std::uint32_t>((z * n + y) * n + x), static_cast<std::uint32_t>(kept));
                    }
        }
        traced.Segments.resize(kept);
        std::ranges::sort(entries);
        std::size_t occupied = 0;
        for (std::size_t i = 0; i < entries.size(); ++i) occupied += i == 0 || entries[i].first != entries[i - 1].first;
        std::size_t size = 16;
        while (size < 2 * occupied && size < kMaxTrailCells) size *= 2;
        traced.Cells.assign(size, TrailCell{});
        traced.CellSegments.reserve(entries.size());
        const auto mask = static_cast<std::uint32_t>(size - 1);
        std::size_t inserted = 0;
        for (std::size_t i = 0; i < entries.size();)
        {
            std::size_t j = i;
            while (j < entries.size() && entries[j].first == entries[i].first) ++j;
            if (inserted + 1 > size / 2) break; // keep the table at most half full
            std::uint32_t slot = TrailHash(entries[i].first, mask);
            while (traced.Cells[slot].Key != kEmptyTrailCell) slot = (slot + 1) & mask;
            traced.Cells[slot] = TrailCell{entries[i].first, static_cast<std::uint32_t>(traced.CellSegments.size()),
                                           static_cast<std::uint32_t>(j - i), 0u};
            for (std::size_t k = i; k < j; ++k) traced.CellSegments.push_back(entries[k].second);
            ++inserted;
            i = j;
        }
    }

    [[nodiscard]] inline TracedTrails SplitTrailsForTracer(const std::vector<RenderSegment>& segments, const glm::vec3& holePosition,
                                                          const glm::vec3& spinAxis, const float radius, const float pad,
                                                          std::vector<RenderSegment>& outside)
    {
        TracedTrails traced;
        BuildTracedTrails(traced, segments, holePosition, spinAxis, radius, pad, outside);
        return traced;
    }

    // Closest distance between segments p0-p1 and q0-q1; `s` is the parameter of the closest point on p0-p1.
    // Mirrors segmentDistance in shaders/trails.glsl.
    [[nodiscard]] inline float SegmentDistance(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& q0, const glm::vec3& q1, float& s)
    {
        const glm::vec3 d1 = p1 - p0;
        const glm::vec3 d2 = q1 - q0;
        const glm::vec3 r = p0 - q0;
        const float a = glm::dot(d1, d1);
        const float e = glm::dot(d2, d2);
        const float f = glm::dot(d2, r);
        float t = 0.0f;
        s = 0.0f;
        if (a <= 1e-12f && e <= 1e-12f) return glm::length(r);
        if (a <= 1e-12f)
        {
            t = std::clamp(f / e, 0.0f, 1.0f);
        }
        else
        {
            const float c = glm::dot(d1, r);
            if (e <= 1e-12f)
            {
                s = std::clamp(-c / a, 0.0f, 1.0f);
            }
            else
            {
                const float b = glm::dot(d1, d2);
                const float denom = a * e - b * b;
                s = denom > 1e-12f ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
                t = (b * s + f) / e;
                if (t < 0.0f) { t = 0.0f; s = std::clamp(-c / a, 0.0f, 1.0f); }
                else if (t > 1.0f) { t = 1.0f; s = std::clamp((b - c) / a, 0.0f, 1.0f); }
            }
        }
        return glm::length(p0 + d1 * s - (q0 + d2 * t));
    }

    struct TrailSample
    {
        float Coverage{0.0f}; // 0..1, how much of the ray the line covers
        glm::vec3 Color{0.0f};
    };

    // Nearest segment to the sub-chord p0-p1 (at most one fine cell long) relative to its half-width, from the fine
    // cell of its midpoint; updates best / bestIndex.
    inline void SearchTrails(const TracedTrails& trails, const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& camera,
                             const float angularHalfWidth, float& best, int& bestIndex)
    {
        constexpr int n = kTrailGridCells;
        const float radius = trails.Radius;
        const glm::vec3 mid = 0.5f * (p0 + p1);
        if (glm::any(glm::greaterThan(glm::abs(mid), glm::vec3(radius)))) return;
        const auto cell = [&](const float v) { return std::clamp(static_cast<int>(std::floor((v + radius) / (2.0f * radius) * n)), 0, n - 1); };
        const int slot = FindTrailCell(trails.Cells, static_cast<std::uint32_t>((cell(mid.z) * n + cell(mid.y)) * n + cell(mid.x)));
        if (slot < 0) return;
        const auto& found = trails.Cells[static_cast<std::size_t>(slot)];
        for (std::uint32_t k = found.Start; k < found.Start + found.Count; ++k)
        {
            const auto& segment = trails.Segments[trails.CellSegments[k]];
            float s = 0.0f;
            const float d = SegmentDistance(p0, p1, glm::vec3(segment.A), glm::vec3(segment.B), s);
            const float width = angularHalfWidth * std::max(glm::length(glm::mix(p0, p1, s) - camera), 1e-6f);
            if (d / width < best)
            {
                best = d / width;
                bestIndex = static_cast<int>(trails.CellSegments[k]);
            }
        }
    }

    // What one ray step (chord p0-p1, hole-local) sees of the trails: the nearest segment within a half-width of
    // `angularHalfWidth` times the distance to the camera, with a soft edge. Long chords are split into pieces of
    // at most one fine cell. Reference for trailSample in shaders/trails.glsl, which does the same per integration step.
    [[nodiscard]] inline TrailSample SampleTrails(const TracedTrails& trails, const glm::vec3& p0, const glm::vec3& p1,
                                                  const glm::vec3& camera, const float angularHalfWidth)
    {
        TrailSample sample;
        if (trails.Segments.empty() || !CoarseTrailHit(trails, glm::min(p0, p1), glm::max(p0, p1))) return sample;
        const float cellSize = 2.0f * trails.Radius / static_cast<float>(kTrailGridCells);
        const int pieces = std::clamp(static_cast<int>(std::ceil(glm::length(p1 - p0) / cellSize)), 1, 128);
        float best = 1e30f;
        int bestIndex = -1;
        for (int i = 0; i < pieces; ++i)
        {
            SearchTrails(trails, glm::mix(p0, p1, static_cast<float>(i) / pieces), glm::mix(p0, p1, static_cast<float>(i + 1) / pieces),
                         camera, angularHalfWidth, best, bestIndex);
        }
        if (bestIndex < 0 || best >= 1.0f) return sample;
        const float edge = std::clamp((best - 0.6f) / 0.4f, 0.0f, 1.0f);
        sample.Coverage = 1.0f - edge * edge * (3.0f - 2.0f * edge); // 1 - smoothstep(0.6, 1, best)
        sample.Color = glm::vec3(trails.Segments[static_cast<std::size_t>(bestIndex)].Color);
        return sample;
    }

    inline std::vector<RenderSegment> SimplifyTrails(const std::vector<RenderSegment>& segments, const float tolerance)
    {
        std::vector<RenderSegment> out;
        SimplifyTrails(segments, tolerance, out);
        return out;
    }

    inline void SimplifyTrails(const std::vector<RenderSegment>& segments, const float tolerance, std::vector<RenderSegment>& out)
    {
        out.clear();
        out.reserve(segments.size());
        thread_local std::vector<glm::vec3> inner; // points the current run would drop
        inner.clear();
        const auto distanceToLine = [](const glm::vec3& p, const glm::vec3& a, const glm::vec3& b)
        {
            const glm::vec3 ab = b - a;
            const float length2 = glm::dot(ab, ab);
            const float t = length2 > 0.0f ? std::clamp(glm::dot(p - a, ab) / length2, 0.0f, 1.0f) : 0.0f;
            return glm::length(p - (a + t * ab));
        };
        for (const auto& segment : segments)
        {
            if (!out.empty() && out.back().B == segment.A && out.back().Color == segment.Color && inner.size() < 64)
            {
                RenderSegment joined{out.back().A, segment.B, segment.Color};
                bool fits = distanceToLine(segment.A, joined.A, joined.B) <= tolerance;
                for (const auto& p : inner) fits = fits && distanceToLine(p, joined.A, joined.B) <= tolerance;
                if (fits)
                {
                    inner.push_back(segment.A);
                    out.back() = joined;
                    continue;
                }
            }
            inner.clear();
            out.push_back(segment);
        }
    }
}
