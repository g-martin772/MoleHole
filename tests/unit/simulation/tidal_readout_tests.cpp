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
    constexpr auto kKerr = static_cast<std::int32_t>(MetricKind::Kerr);

    struct TidalScene
    {
        Scene World{"Tidal"};
        HoleFrame Frame{};
    };

    TidalScene MakeScene(const double spin)
    {
        RegisterComponents();
        TidalScene test;
        const BlackHoleComponent hole{.Mass = 1e6 * Units::kSolarMass, .Spin = spin};
        const auto entity = test.World.CreateEntity("Hole");
        test.World.Registry().emplace<TransformComponent>(entity);
        test.World.Registry().emplace<BlackHoleComponent>(entity, hole);
        test.Frame = MakeHoleFrame(hole, {0.0, 0.0, 0.0});
        return test;
    }

    // A Geodesic Body on the circular orbit at BL r (in M) in the equatorial plane, launched by one module tick
    entt::entity AddOrbitingBody(TidalScene& test, const double r, const double sphereRadiusM)
    {
        const auto body = test.World.CreateEntity("Planet");
        test.World.Registry().emplace<TransformComponent>(body, TransformComponent{.Position = BoyerLindquistToWorld(test.Frame, r, std::numbers::pi / 2.0, 0.0)});
        test.World.Registry().emplace<GeodesicBodyComponent>(body, GeodesicBodyComponent{.Launch = GeodesicLaunch::Orbiting});
        if (sphereRadiusM > 0.0) test.World.Registry().emplace<SphereComponent>(body, SphereComponent{.Radius = sphereRadiusM});
        GeodesicSimulationModule module;
        module.OnInit(test.World);
        module.OnTick(test.World, 0.01f);
        return body;
    }

    double SiScale(const TidalScene& test)
    {
        return Units::kSpeedOfLight * Units::kSpeedOfLight / (test.Frame.LengthM * test.Frame.LengthM);
    }
}

TEST_CASE("Tidal readout of a static observer in Schwarzschild", "[simulation][tidal][readout]")
{
    auto test = MakeScene(0.0);
    const double r = 10.0;
    const glm::dvec3 camera = test.Frame.Frame * glm::dvec3(r, 0.0, 0.0) * test.Frame.LengthM;
    const auto readout = ComputeObserverReadout(test.World, kKerr, ObserverKind::Static, OrbitDirection::Prograde, camera, 0);
    REQUIRE(readout.Tidal);
    const double scale = SiScale(test) / (r * r * r);
    CHECK(readout.Tidal->EigenvaluesS2[0] == Approx(-2.0 * scale).epsilon(1e-10));
    CHECK(readout.Tidal->EigenvaluesS2[1] == Approx(scale).epsilon(1e-10));
    CHECK(readout.Tidal->EigenvaluesS2[2] == Approx(scale).epsilon(1e-10));
    // The stretch points at the hole
    const glm::dvec3 radial = glm::normalize(camera);
    CHECK(std::abs(glm::dot(readout.Tidal->AxesWorld[0], radial)) == Approx(1.0).epsilon(1e-12));
    CHECK(TidalStretchAcrossMs2(*readout.Tidal, 2.0) == Approx(4.0 * scale));

    // Inside the horizon there is nothing to report
    const auto inside = ComputeObserverReadout(test.World, kKerr, ObserverKind::Static, OrbitDirection::Prograde,
                                               0.5 * test.Frame.Frame * glm::dvec3(1.0, 0.0, 0.0) * test.Frame.LengthM, 0);
    CHECK_FALSE(inside.Tidal);
}

TEST_CASE("A free-falling Geodesic Body feels the tensor of its geodesic", "[simulation][tidal][readout]")
{
    auto test = MakeScene(0.9);
    const auto body = AddOrbitingBody(test, 6.0, 0.0);
    const auto& component = test.World.Registry().get<const GeodesicBodyComponent>(body);
    REQUIRE(component.Status == GeodesicBodyStatus::FreeFall);
    const auto sample = BodyTidalSample(test.World, body);
    REQUIRE(sample);
    const auto expected = PrincipalAxes(TidalTensor(test.Frame.Metric, component.State));
    for (std::size_t k = 0; k < 3; ++k)
    {
        CHECK(sample->EigenvaluesS2[k] == Approx(expected.Eigenvalues[k] * SiScale(test)).epsilon(1e-12));
        CHECK(glm::length(sample->AxesWorld[k]) == Approx(1.0));
    }
    // Squeezed perpendicular to the orbital plane, which is the hole's equator (spin axis along world +Y)
    CHECK(std::abs(glm::dot(sample->AxesWorld[2], test.Frame.Frame[2])) == Approx(1.0).epsilon(1e-6));

    // A camera riding the body reads the same tensor
    const auto readout = ComputeObserverReadout(test.World, kKerr, ObserverKind::Zamo, OrbitDirection::Prograde, {},
                                                test.World.GuidOf(body));
    REQUIRE(readout.Tidal);
    CHECK(readout.Tidal->EigenvaluesS2[0] == Approx(sample->EigenvaluesS2[0]));

    // Not launched: no tensor
    const auto idle = test.World.CreateEntity("Idle");
    test.World.Registry().emplace<GeodesicBodyComponent>(idle);
    CHECK_FALSE(BodyTidalSample(test.World, idle));
}

TEST_CASE("Tidal glyphs", "[rendering][tidal][overlay]")
{
    auto test = MakeScene(0.0);
    const double radiusM = 1.0e5;
    const auto body = AddOrbitingBody(test, 8.0, radiusM);
    const auto segments = TidalAxesOverlay(test.World, 3.0);
    // Three axes, each a line and two arrowheads of two strokes
    REQUIRE(segments.size() == 15);
    const glm::dvec3 center = test.World.Registry().get<const TransformComponent>(body).Position;
    double longest = 0.0;
    int stretch = 0;
    for (const auto& segment : segments)
    {
        if (glm::length(segment.A + segment.B - 2.0 * center) < 1e-6 * radiusM)
        {
            longest = std::max(longest, glm::length(segment.B - segment.A) / 2.0);
            if (segment.Color == kTidalStretchColor) ++stretch;
        }
    }
    // The strongest axis is `scale` radii long, and a circular orbit has exactly one stretching axis
    CHECK(longest == Approx(3.0 * radiusM));
    CHECK(stretch == 1);
    // The body a riding camera sits on gets no glyph
    CHECK(TidalAxesOverlay(test.World, 3.0, test.World.GuidOf(body)).empty());
}
