#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

namespace
{
    constexpr double kHalfPi = std::numbers::pi / 2.0;

    struct HoleScene
    {
        Scene World{"Overlays"};
        HoleFrame Frame{};
    };

    HoleScene MakeHoleScene(const double spin)
    {
        RegisterComponents();
        HoleScene test;
        const BlackHoleComponent hole{.Mass = 1e6 * Units::kSolarMass, .Spin = spin,
                                      .SpinAxis = glm::normalize(glm::vec3(0.2f, 1.0f, -0.3f))};
        const glm::dvec3 position{1e10, 2e10, -3e10};
        const auto entity = test.World.CreateEntity("Hole");
        test.World.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = position});
        test.World.Registry().emplace<BlackHoleComponent>(entity, hole);
        test.Frame = MakeHoleFrame(hole, position);
        return test;
    }

    std::vector<OverlaySegment> WithColor(const std::vector<OverlaySegment>& all, const glm::vec3& color)
    {
        std::vector<OverlaySegment> result;
        for (const auto& segment : all)
        {
            if (glm::length(segment.Color - color) < 1e-6f) result.push_back(segment);
        }
        return result;
    }
}

TEST_CASE("Characteristic-radius overlays lie on the right Boyer-Lindquist radii", "[rendering][overlay]")
{
    auto test = MakeHoleScene(0.9);
    const auto segments = CharacteristicRadiiOverlay(test.World, 96);
    const auto& m = test.Frame.Metric;

    const auto checkRing = [&](const glm::vec3& color, const double radius)
    {
        const auto ring = WithColor(segments, color);
        INFO("radius " << radius);
        REQUIRE(ring.size() == 96);
        for (const auto& segment : ring)
        {
            const auto bl = WorldToBoyerLindquist(test.Frame, segment.A);
            CHECK(bl.x == Approx(radius).epsilon(1e-9));
            CHECK(bl.y == Approx(kHalfPi).margin(1e-9));
        }
    };
    checkRing(kIscoOverlayColor, IscoRadius(m, OrbitDirection::Prograde));
    checkRing(kIscoOverlayColor * kRetrogradeOverlayDimming, IscoRadius(m, OrbitDirection::Retrograde));
    checkRing(kPhotonOrbitOverlayColor, PhotonOrbitRadius(m, OrbitDirection::Prograde));
    checkRing(kPhotonOrbitOverlayColor * kRetrogradeOverlayDimming, PhotonOrbitRadius(m, OrbitDirection::Retrograde));
    checkRing(kMarginallyBoundOverlayColor, MarginallyBoundRadius(m, OrbitDirection::Prograde));
    checkRing(kMarginallyBoundOverlayColor * kRetrogradeOverlayDimming, MarginallyBoundRadius(m, OrbitDirection::Retrograde));

    // Horizon: every point at r+; ergosphere: on the static limit r_E(θ), touching the horizon at the poles
    for (const auto& segment : WithColor(segments, kHorizonOverlayColor))
    {
        CHECK(WorldToBoyerLindquist(test.Frame, segment.A).x == Approx(OuterHorizonRadius(m)).epsilon(1e-9));
    }
    const auto ergosphere = WithColor(segments, kErgosphereOverlayColor);
    REQUIRE_FALSE(ergosphere.empty());
    for (const auto& segment : ergosphere)
    {
        const auto bl = WorldToBoyerLindquist(test.Frame, segment.B);
        CHECK(bl.x == Approx(ErgosphereRadius(m, bl.y)).epsilon(1e-6));
    }
}

TEST_CASE("Predicted orbits follow the integrator and stop at the horizon", "[rendering][overlay][geodesic]")
{
    auto test = MakeHoleScene(0.6);
    const auto add = [&](const glm::dvec3& localM, const GeodesicLaunch launch)
    {
        const auto entity = test.World.CreateEntity("Body");
        test.World.Registry().emplace<TransformComponent>(
            entity, TransformComponent{.Position = test.Frame.Position + test.Frame.Frame * localM * test.Frame.LengthM});
        test.World.Registry().emplace<GeodesicBodyComponent>(entity, GeodesicBodyComponent{.Launch = launch});
        return entity;
    };
    const auto orbiting = add({10.0, 0.0, 0.0}, GeodesicLaunch::Orbiting);
    GeodesicSimulationModule module;
    module.OnInit(test.World);

    constexpr double kSpan = 200.0;
    constexpr int kSamples = 100;
    const auto segments = PredictedOrbitsOverlay(test.World, kSpan, kSamples);
    REQUIRE(segments.size() == kSamples);

    // The circular orbit's trail stays at its radius and ends where the integrator puts it after the span
    const auto& body = test.World.Registry().get<GeodesicBodyComponent>(orbiting);
    auto reference = body.State;
    AdvanceToCoordinateTime(test.Frame.Metric, reference, kSpan);
    const auto expected = BoyerLindquistToWorld(test.Frame, reference.R, reference.Theta, reference.Phi);
    const auto end = segments[kSamples - 1].B;
    CHECK(glm::length(end - expected) < 1e-6 * glm::length(expected - test.Frame.Position));
    for (int i = 0; i < kSamples; ++i)
    {
        CHECK(WorldToBoyerLindquist(test.Frame, segments[static_cast<std::size_t>(i)].B).x == Approx(body.State.R).epsilon(1e-7));
    }
    // Bright at the start, dim at the end
    CHECK(segments.front().Color.r > segments[kSamples - 1].Color.r);

    // The prediction does not touch the scene's own state
    CHECK(test.World.Registry().get<GeodesicBodyComponent>(orbiting).State.T == 0.0);

    // A raindrop's trail ends at the horizon, after far fewer samples
    add({0.0, 8.0, 0.0}, GeodesicLaunch::Raindrop);
    module.OnInit(test.World);
    const auto both = PredictedOrbitsOverlay(test.World, kSpan, kSamples);
    CHECK(both.size() > kSamples);
    CHECK(both.size() < 2 * kSamples);
}

TEST_CASE("Overlay settings are registered and hidden by default", "[rendering][overlay][settings]")
{
    const RenderSettings defaults;
    CHECK_FALSE(defaults.ShowCharacteristicRadii);
    CHECK_FALSE(defaults.ShowPredictedOrbits);
    const auto& registry = RenderSettingRegistry();
    CHECK(registry.Find("overlay.characteristic_radii") != nullptr);
    CHECK(registry.Find("overlay.predicted_orbits") != nullptr);
    CHECK(registry.Find("overlay.prediction_span") != nullptr);
}

TEST_CASE("Overlay points are aberrated exactly as the tracer maps a moving camera's pixels", "[rendering][overlay][observer]")
{
    auto test = MakeHoleScene(0.9);
    const glm::dvec3 camera = test.Frame.Position + test.Frame.Frame * glm::dvec3(9.0, 2.0, 3.0) * test.Frame.LengthM;
    const CameraObserverFrame observer{.Velocity = {0.1, -0.2, 0.45}, .Hole = test.Frame};
    const CameraObserverFrame still{.Velocity = {0.0, 0.0, 0.0}, .Hole = test.Frame};

    // The shader's per-pixel map (rk4RayMarching): camera look direction -> direction the local ZAMO sees
    const auto basis = [&]
    {
        const auto bl = WorldToBoyerLindquist(test.Frame, camera);
        const double a = test.Frame.Metric.SpinParameter;
        const double r = bl.x, s = std::sin(bl.y), c = std::cos(bl.y), rho = std::sqrt(r * r + a * a);
        return std::array<glm::dvec3, 3>{
            test.Frame.Frame * glm::normalize(glm::dvec3(r / rho * s * std::cos(bl.z), r / rho * s * std::sin(bl.z), c)),
            test.Frame.Frame * glm::normalize(glm::dvec3(rho * c * std::cos(bl.z), rho * c * std::sin(bl.z), -r * s)),
            test.Frame.Frame * glm::dvec3(-std::sin(bl.z), std::cos(bl.z), 0.0)};
    }();
    const auto shaderMap = [&](const glm::dvec3& lookCamera)
    {
        const Vector3 k{-glm::dot(lookCamera, basis[0]), -glm::dot(lookCamera, basis[1]), -glm::dot(lookCamera, basis[2])};
        const auto z = BoostPhoton(observer.Velocity, k).Direction;
        return glm::normalize(-(z[0] * basis[0] + z[1] * basis[1] + z[2] * basis[2]));
    };

    for (const glm::dvec3 local : {glm::dvec3(0.0), glm::dvec3(20.0, -5.0, 1.0), glm::dvec3(-3.0, 15.0, -8.0)})
    {
        const glm::dvec3 point = test.Frame.Position + test.Frame.Frame * local * test.Frame.LengthM;
        const glm::dvec3 apparent = AberrateForCamera(observer, camera, point);
        // Same distance, and the tracer maps the apparent direction back onto the true one
        CHECK(glm::length(apparent - camera) == Approx(glm::length(point - camera)).epsilon(1e-12));
        const glm::dvec3 recovered = shaderMap(glm::normalize(apparent - camera));
        CHECK(glm::length(recovered - glm::normalize(point - camera)) < 1e-12);
        // A camera at rest relative to the ZAMO sees no shift
        CHECK(glm::length(AberrateForCamera(still, camera, point) - point) < 1e-9 * glm::length(point - camera));
    }
}

TEST_CASE("A riding camera does not draw its own predicted trail", "[rendering][overlay][geodesic]")
{
    auto test = MakeHoleScene(0.6);
    const auto entity = test.World.CreateEntity("Camera body");
    test.World.Registry().emplace<TransformComponent>(
        entity, TransformComponent{.Position = test.Frame.Position + test.Frame.Frame * glm::dvec3(12.0, 0.0, 0.0) * test.Frame.LengthM});
    test.World.Registry().emplace<GeodesicBodyComponent>(entity, GeodesicBodyComponent{.Launch = GeodesicLaunch::Orbiting});
    GeodesicSimulationModule module;
    module.OnInit(test.World);
    CHECK(PredictedOrbitsOverlay(test.World, 100.0, 50).size() == 50);
    CHECK(PredictedOrbitsOverlay(test.World, 100.0, 50, test.World.GuidOf(entity)).empty());
}

TEST_CASE("Orbit overlay legends match the drawn colours", "[rendering][overlay][legend]")
{
    const auto same = [](const LegendColor& legend, const glm::vec3& drawn)
    {
        return legend.R == Approx(drawn.r) && legend.G == Approx(drawn.g) && legend.B == Approx(drawn.b);
    };
    CHECK(same(kCharacteristicRadiiLegend[0].Color, kHorizonOverlayColor));
    CHECK(same(kCharacteristicRadiiLegend[1].Color, kErgosphereOverlayColor));
    CHECK(same(kCharacteristicRadiiLegend[2].Color, kPhotonOrbitOverlayColor));
    CHECK(same(kCharacteristicRadiiLegend[3].Color, kMarginallyBoundOverlayColor));
    CHECK(same(kCharacteristicRadiiLegend[4].Color, kIscoOverlayColor));
    CHECK(same(kPredictedOrbitLegend[0].Color, kPredictionNearColor));
    CHECK(same(kPredictedOrbitLegend[1].Color, kPredictionFarColor));
}

TEST_CASE("The integrated tracer gets the first hole's ring radii", "[rendering][overlay]")
{
    RaytraceParamsGpu params{};
    params.NumBlackHoles = 1;
    params.MetricType = static_cast<std::int32_t>(MetricKind::Kerr);
    params.BlackHoles[0].Mass = 2.0f; // render units
    params.BlackHoles[0].Spin = 0.9f;
    FillMetricData(params);

    FillOrbitOverlayData(params, false);
    CHECK(params.OrbitRadii2.z == 0.0f);

    FillOrbitOverlayData(params, true);
    const KerrNewmanParams m{.Mass = 2.0, .SpinParameter = 2.0 * static_cast<double>(0.9f)};
    CHECK(params.OrbitRadii2.z == 1.0f);
    CHECK(params.OrbitRadii.x == Approx(PhotonOrbitRadius(m, OrbitDirection::Prograde)).epsilon(1e-5));
    CHECK(params.OrbitRadii.y == Approx(PhotonOrbitRadius(m, OrbitDirection::Retrograde)).epsilon(1e-5));
    CHECK(params.OrbitRadii.z == Approx(MarginallyBoundRadius(m, OrbitDirection::Prograde)).epsilon(1e-5));
    CHECK(params.OrbitRadii.w == Approx(MarginallyBoundRadius(m, OrbitDirection::Retrograde)).epsilon(1e-5));
    CHECK(params.OrbitRadii2.x == Approx(IscoRadius(m, OrbitDirection::Prograde)).epsilon(1e-5));
    CHECK(params.OrbitRadii2.y == Approx(IscoRadius(m, OrbitDirection::Retrograde)).epsilon(1e-5));
    // The tracer's own ISCO (disk inner edge) is the same ring
    CHECK(params.OrbitRadii2.x == Approx(params.BlackHoles[0].IscoRadius));
}

TEST_CASE("Traced and rasterised rings share their colours, and the traced hole is not drawn twice", "[rendering][overlay]")
{
    std::ifstream file(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "shaders" / "debug_view.glsl");
    const std::string shader{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const auto color = [&](const std::string& name)
    {
        const auto at = shader.find("const vec3 " + name + " = vec3(");
        REQUIRE(at != std::string::npos);
        std::istringstream values(shader.substr(shader.find('(', at) + 1));
        float r = 0, g = 0, b = 0;
        char comma = 0;
        values >> r >> comma >> g >> comma >> b;
        return glm::vec3(r, g, b);
    };
    CHECK(glm::length(color("ORBIT_PHOTON_COLOR") - kPhotonOrbitOverlayColor) < 1e-6f);
    CHECK(glm::length(color("ORBIT_BOUND_COLOR") - kMarginallyBoundOverlayColor) < 1e-6f);
    CHECK(glm::length(color("ORBIT_ISCO_COLOR") - kIscoOverlayColor) < 1e-6f);
    CHECK(glm::length(color("ORBIT_ERGOSPHERE_COLOR") - kErgosphereOverlayColor) < 1e-6f);
    CHECK(shader.contains("ORBIT_RETROGRADE_DIMMING = 0.5"));

    auto test = MakeHoleScene(0.5);
    const auto primary = PrimaryHole(test.World);
    REQUIRE(primary);
    CHECK_FALSE(CharacteristicRadiiOverlay(test.World).empty());
    CHECK(CharacteristicRadiiOverlay(test.World, 96, primary->first).empty());
}

namespace
{
    std::string ReadShader(const char* name)
    {
        std::ifstream file(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "shaders" / name);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
}

TEST_CASE("Trail tracing constants agree between C++ and the shaders", "[rendering][overlay][trails]")
{
    CHECK(ReadShader("ray_tracing.glsl").contains(std::format("INTEGRATION_RADIUS_M = {:.1f}f;", kTracerIntegrationRadius)));
    CHECK(ReadShader("trails.glsl").contains(std::format("TRAIL_GRID_CELLS = {};", kTrailGridCells)));
    // The shader's spin frame is the geodesic module's
    const glm::vec3 axis = glm::normalize(glm::vec3(0.2f, 1.0f, -0.3f));
    const auto frame = ShaderSpinFrame(axis);
    const auto hole = MakeHoleFrame(BlackHoleComponent{.SpinAxis = axis}, {0.0, 0.0, 0.0});
    for (int c = 0; c < 3; ++c) CHECK(glm::length(glm::dvec3(frame[c]) - hole.Frame[c]) < 1e-6);
}

TEST_CASE("Trails are split at the integration sphere and moved into the hole's frame", "[rendering][overlay][trails]")
{
    const glm::vec3 center{100.0f, -20.0f, 5.0f};
    const glm::vec3 axis{0.0f, 1.0f, 0.0f};
    const glm::mat3 toLocal = glm::transpose(ShaderSpinFrame(axis));
    const glm::vec3 color{1.0f, 0.5f, 0.0f};
    const std::vector<RenderSegment> segments{
        {center + glm::vec3(10.0f, 0.0f, 0.0f), center + glm::vec3(0.0f, 10.0f, 0.0f), color}, // inside
        {center + glm::vec3(-80.0f, 0.0f, 0.0f), center + glm::vec3(80.0f, 0.0f, 0.0f), color}, // through: out-in-out
        {center + glm::vec3(0.0f, 70.0f, 0.0f), center + glm::vec3(0.0f, 90.0f, 0.0f), color},  // outside
    };
    std::vector<RenderSegment> outside;
    const auto traced = SplitTrailsForTracer(segments, center, axis, 50.0f, 0.1f, outside);
    REQUIRE(traced.Segments.size() == 2);
    CHECK(outside.size() == 3); // two ends of the through segment and the outer one
    CHECK(glm::length(glm::vec3(traced.Segments[0].A) - toLocal * glm::vec3(10.0f, 0.0f, 0.0f)) < 1e-5f);
    // The through segment is cut exactly at the sphere
    CHECK(glm::length(glm::vec3(traced.Segments[1].A)) == Approx(50.0f).epsilon(1e-5));
    CHECK(glm::length(glm::vec3(traced.Segments[1].B)) == Approx(50.0f).epsilon(1e-5));
    CHECK(glm::length(outside[0].B - center) == Approx(50.0f).epsilon(1e-5));
    // Hash table: power-of-two size, at most half full, and every occupied cell's range inside CellSegments
    const auto size = traced.Cells.size();
    CHECK((size & (size - 1)) == 0);
    std::size_t occupied = 0;
    std::size_t entries = 0;
    for (const auto& cell : traced.Cells)
    {
        if (cell.Key == kEmptyTrailCell) continue;
        ++occupied;
        entries += cell.Count;
        CHECK(FindTrailCell(traced.Cells, cell.Key) == static_cast<int>(&cell - traced.Cells.data()));
        CHECK(cell.Start + cell.Count <= traced.CellSegments.size());
    }
    CHECK(2 * occupied <= size);
    CHECK(entries == traced.CellSegments.size());
    CHECK(FindTrailCell(traced.Cells, 12345u) == -1);
}

TEST_CASE("The trail grid finds exactly what a search over all segments finds", "[rendering][overlay][trails]")
{
    // A curly predicted-orbit-like trail around the origin
    std::vector<RenderSegment> segments;
    for (int i = 0; i < 400; ++i)
    {
        const auto point = [](const float t) { return glm::vec3(12.0f * std::cos(t), 3.0f * std::sin(3.0f * t), 9.0f * std::sin(t)); };
        segments.push_back({point(0.02f * i), point(0.02f * (i + 1)), glm::vec3(0.1f * (i % 10), 1.0f, 0.0f)});
    }
    const glm::vec3 camera{0.0f, 15.0f, 40.0f};
    const float halfWidth = 0.004f;
    std::vector<RenderSegment> outside;
    const auto traced = SplitTrailsForTracer(segments, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 50.0f,
                                             halfWidth * 90.0f, outside);
    REQUIRE(traced.Segments.size() == segments.size());

    std::mt19937 random(7);
    std::uniform_real_distribution<float> coordinate(-15.0f, 15.0f);
    std::uniform_real_distribution<float> offset(-0.4f, 0.4f);
    int hits = 0;
    for (int trial = 0; trial < 4000; ++trial)
    {
        // Short chords near the trail, like integration steps
        const auto& near = traced.Segments[static_cast<std::size_t>(trial) % traced.Segments.size()];
        const glm::vec3 p0 = glm::vec3(near.A) + glm::vec3(offset(random), offset(random), offset(random));
        const glm::vec3 p1 = p0 + glm::vec3(offset(random), offset(random), offset(random));
        const auto sample = SampleTrails(traced, p0, p1, camera, halfWidth);

        float best = 1e30f;
        for (const auto& segment : traced.Segments)
        {
            float s = 0.0f;
            const float d = SegmentDistance(p0, p1, glm::vec3(segment.A), glm::vec3(segment.B), s);
            best = std::min(best, d / (halfWidth * glm::length(glm::mix(p0, p1, s) - camera)));
        }
        INFO("trial " << trial);
        CHECK((sample.Coverage > 0.0f) == (best < 1.0f));
        hits += sample.Coverage > 0.0f;
        (void)coordinate;
    }
    CHECK(hits > 50); // the test exercises real hits, not only misses
}

TEST_CASE("Trail lines keep a constant width on screen", "[rendering][overlay][trails]")
{
    const std::vector<RenderSegment> segments{{glm::vec3(-5.0f, 0.0f, 0.0f), glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(1.0f)}};
    std::vector<RenderSegment> outside;
    // Spin axis z: the hole's local frame is the identity, so chords and camera can be given in world coordinates
    const auto traced = SplitTrailsForTracer(segments, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 50.0f, 1.0f, outside);
    REQUIRE(glm::length(glm::vec3(traced.Segments[0].A) - segments[0].A) < 1e-6f);
    const float halfWidth = 0.01f; // radians
    for (const float distance : {5.0f, 20.0f})
    {
        // Chords parallel to the camera's view, passing the line at a perpendicular offset y
        const glm::vec3 camera{0.0f, 0.0f, distance};
        const auto at = [&](const float y) { return SampleTrails(traced, glm::vec3(0.0f, y, 0.5f), glm::vec3(0.0f, y, -0.5f), camera, halfWidth).Coverage; };
        CHECK(at(0.0f) == Approx(1.0f));
        CHECK(at(0.5f * halfWidth * distance) == Approx(1.0f));
        CHECK(at(1.1f * halfWidth * distance) == 0.0f);
    }
    CHECK(SegmentDistance({0, 0, 0}, {1, 0, 0}, {0.5f, 2.0f, 0}, {0.5f, 2.0f, 1}, *std::make_unique<float>()) == Approx(2.0f));
}

TEST_CASE("Long ray steps are split so they still find thin trails", "[rendering][overlay][trails]")
{
    // A chord much longer than a cell passing a short trail near its middle
    const std::vector<RenderSegment> segments{{glm::vec3(0.0f, -0.2f, 0.0f), glm::vec3(0.0f, 0.2f, 0.0f), glm::vec3(1.0f)}};
    std::vector<RenderSegment> outside;
    const auto traced = SplitTrailsForTracer(segments, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 50.0f, 0.1f, outside);
    const glm::vec3 camera{0.0f, 0.0f, 30.0f};
    CHECK(SampleTrails(traced, glm::vec3(-20.0f, 0.0f, 0.0f), glm::vec3(20.0f, 0.0f, 0.0f), camera, 0.002f).Coverage == Approx(1.0f));
}

TEST_CASE("Dense trail polylines are thinned without moving them", "[rendering][overlay][trails]")
{
    // 1000 points on a circle of radius 10 (spacing 0.06) and a straight run of 100 collinear points
    std::vector<RenderSegment> segments;
    const auto circle = [](const int i) { const float t = 0.00628f * i; return glm::vec3(10.0f * std::cos(t), 0.0f, 10.0f * std::sin(t)); };
    for (int i = 0; i < 1000; ++i) segments.push_back({circle(i), circle(i + 1), glm::vec3(1.0f, 0.0f, 0.0f)});
    for (int i = 0; i < 100; ++i) segments.push_back({glm::vec3(20.0f + 0.1f * i, 0.0f, 0.0f), glm::vec3(20.1f + 0.1f * i, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)});
    const auto thinned = SimplifyTrails(segments, 0.01f);
    CHECK(thinned.size() < segments.size() / 5);
    // Every original vertex lies within the tolerance of the thinned polyline of its colour
    for (const auto& original : segments)
    {
        float nearest = 1e30f;
        for (const auto& kept : thinned)
        {
            if (kept.Color != original.Color) continue;
            float s = 0.0f;
            nearest = std::min(nearest, SegmentDistance(original.A, original.A, kept.A, kept.B, s));
        }
        CHECK(nearest <= 0.0101f);
    }
}

TEST_CASE("Rebuilding trails into reused storage matches a fresh build", "[rendering][overlay][trails]")
{
    const auto make = [](const int count, const float offset)
    {
        std::vector<RenderSegment> segments;
        for (int i = 0; i < count; ++i)
        {
            const float t = 0.05f * static_cast<float>(i);
            segments.push_back({glm::vec3(8.0f * std::cos(t), offset, 8.0f * std::sin(t)),
                                glm::vec3(8.0f * std::cos(t + 0.05f), offset, 8.0f * std::sin(t + 0.05f)), glm::vec3(1.0f, 0.5f, 0.0f)});
        }
        return segments;
    };
    const glm::vec3 axis(0.0f, 1.0f, 0.0f);
    TracedTrails reused;
    std::vector<RenderSegment> outsideReused;
    BuildTracedTrails(reused, make(400, 1.0f), glm::vec3(0.0f), axis, 50.0f, 0.1f, outsideReused);

    outsideReused.clear();
    const auto second = make(60, -2.0f);
    BuildTracedTrails(reused, second, glm::vec3(0.0f), axis, 50.0f, 0.1f, outsideReused);
    std::vector<RenderSegment> outsideFresh;
    const auto fresh = SplitTrailsForTracer(second, glm::vec3(0.0f), axis, 50.0f, 0.1f, outsideFresh);

    CHECK(reused.Segments.size() == fresh.Segments.size());
    CHECK(std::ranges::equal(reused.Cells, fresh.Cells, [](const TrailCell& a, const TrailCell& b)
                             { return a.Key == b.Key && a.Start == b.Start && a.Count == b.Count; }));
    CHECK(reused.CellSegments == fresh.CellSegments);
    CHECK(reused.Coarse == fresh.Coarse);
    CHECK(outsideReused.size() == outsideFresh.size());
}

TEST_CASE("Simplifying into a buffer matches the returning overload", "[rendering][overlay][trails]")
{
    std::vector<RenderSegment> segments;
    for (int i = 0; i < 300; ++i)
        segments.push_back({glm::vec3(0.1f * i, 0.0f, 0.0f), glm::vec3(0.1f * (i + 1), 0.0f, 0.0f), glm::vec3(1.0f)});
    std::vector<RenderSegment> out(5);
    SimplifyTrails(segments, 0.01f, out);
    const auto returned = SimplifyTrails(segments, 0.01f);
    REQUIRE(out.size() == returned.size());
    for (std::size_t i = 0; i < out.size(); ++i) CHECK((out[i].A == returned[i].A && out[i].B == returned[i].B));
}
