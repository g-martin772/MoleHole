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
    constexpr auto kKerr = static_cast<std::int32_t>(MetricKind::Kerr);

    struct HoleScene
    {
        Scene World{"Readout"};
        HoleFrame Frame{};
        std::uint64_t HoleGuid{0};
    };

    HoleScene MakeHoleScene(const BlackHoleComponent& hole)
    {
        RegisterComponents();
        HoleScene test;
        const auto entity = test.World.CreateEntity("Hole");
        test.World.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = {0.0, 0.0, 0.0}});
        test.World.Registry().emplace<BlackHoleComponent>(entity, hole);
        test.HoleGuid = test.World.GuidOf(entity);
        test.Frame = MakeHoleFrame(hole, {0.0, 0.0, 0.0});
        return test;
    }

    // World position of local (x, y, z) in units of M in the hole's spin frame
    glm::dvec3 At(const HoleScene& test, const glm::dvec3& local)
    {
        return test.Frame.Position + test.Frame.Frame * local * test.Frame.LengthM;
    }
}

TEST_CASE("Proper acceleration of stationary observers", "[physics][observer][acceleration]")
{
    // Static observer in Schwarzschild: a = M / (r² sqrt(1 - 2M/r)); the ZAMO is the same observer there
    const KerrNewmanParams schwarzschild{.Mass = 1.0};
    for (const double r : {3.0, 10.0, 100.0})
    {
        const double expected = 1.0 / (r * r * std::sqrt(1.0 - 2.0 / r));
        CHECK(*ProperAcceleration(schwarzschild, ObserverKind::Static, r, 1.0) == Approx(expected).epsilon(1e-6));
        CHECK(*ProperAcceleration(schwarzschild, ObserverKind::Zamo, r, 1.0) == Approx(expected).epsilon(1e-6));
    }

    // Free fall: circular geodesics in the plane and raindrops need no thrust
    const KerrNewmanParams kerr{.Mass = 1.0, .SpinParameter = 0.9};
    for (const auto direction : {OrbitDirection::Prograde, OrbitDirection::Retrograde})
    {
        CHECK(*ProperAcceleration(kerr, ObserverKind::Orbiting, 8.0, kHalfPi, direction) == Approx(0.0).margin(1e-8));
    }
    CHECK(*ProperAcceleration(kerr, ObserverKind::Raindrop, 5.0, 1.0) == 0.0);
    // Off the plane the orbiting observer is held on its circle
    CHECK(*ProperAcceleration(kerr, ObserverKind::Orbiting, 8.0, 1.0) > 1e-3);

    // Hovering just outside the static limit takes ever more thrust; inside, it is impossible
    const double ergosphere = ErgosphereRadius(kerr, 1.2);
    CHECK(*ProperAcceleration(kerr, ObserverKind::Static, 1.001 * ergosphere, 1.2) >
          10.0 * *ProperAcceleration(kerr, ObserverKind::Static, 1.5 * ergosphere, 1.2));
    CHECK_FALSE(ProperAcceleration(kerr, ObserverKind::Static, 0.99 * ergosphere, 1.2));
}

TEST_CASE("The observer readout describes the preset observer at the camera", "[simulation][observer][readout]")
{
    auto test = MakeHoleScene(BlackHoleComponent{.Mass = 1e6 * Units::kSolarMass, .Spin = 0.6});
    const KerrNewmanParams metric{.Mass = 1.0, .SpinParameter = 0.6};
    const glm::dvec3 camera = At(test, {std::sqrt(10.0 * 10.0 + 0.36), 0.0, 0.0}); // BL r = 10 M in the plane

    const auto orbiting = ComputeObserverReadout(test.World, kKerr, ObserverKind::Orbiting, OrbitDirection::Prograde, camera, 0);
    CHECK(orbiting.Source == ObserverSource::Preset);
    CHECK(orbiting.Radius == Approx(10.0).epsilon(1e-9));
    CHECK(orbiting.ClockRate == Approx(std::sqrt(CircularOrbitNorm(metric, 10.0, OrbitDirection::Prograde))).epsilon(1e-9));
    CHECK(orbiting.FreeFall);
    CHECK(orbiting.AccelerationMs2 == Approx(0.0).margin(1e-6 * test.Frame.LengthM));

    // Static at r = 10 M: thrust M c² / (r_g r² sqrt(...)) in SI, far from any Earth-like value for 10^6 suns
    const auto still = ComputeObserverReadout(test.World, kKerr, ObserverKind::Static, OrbitDirection::Prograde, camera, 0);
    CHECK_FALSE(still.FreeFall);
    const double geometric = *ProperAcceleration(metric, ObserverKind::Static, 10.0, kHalfPi);
    CHECK(still.AccelerationMs2 == Approx(geometric * Units::kSpeedOfLight * Units::kSpeedOfLight / test.Frame.LengthM));
    CHECK(still.AccelerationMs2 / EarthSurfaceGravity() > 1.0);

    // Inside the ergosphere a static camera is impossible
    const auto inside = ComputeObserverReadout(test.World, kKerr, ObserverKind::Static, OrbitDirection::Prograde,
                                               At(test, {1.9, 0.0, 0.0}), 0);
    CHECK(inside.Source == ObserverSource::Fallback);
    CHECK(inside.Kind == ObserverKind::Zamo);

    Scene empty("Empty");
    CHECK(ComputeObserverReadout(empty, kKerr, ObserverKind::Zamo, OrbitDirection::Prograde, {}, 0).Source ==
          ObserverSource::None);
}

TEST_CASE("A scene camera riding a geodesic body sets the observer frame", "[simulation][observer][readout][rendering]")
{
    auto test = MakeHoleScene(BlackHoleComponent{.Mass = 1e6 * Units::kSolarMass, .Spin = 0.6});
    const auto camera = test.World.CreateEntity("Camera");
    test.World.Registry().emplace<TransformComponent>(camera, TransformComponent{.Position = At(test, {12.0, 0.0, 0.0})});
    test.World.Registry().emplace<CameraComponent>(camera);
    test.World.Registry().emplace<GeodesicBodyComponent>(camera, GeodesicBodyComponent{.Launch = GeodesicLaunch::Orbiting});
    const auto guid = test.World.GuidOf(camera);

    const auto view = FindPrimarySceneCamera(test.World);
    REQUIRE(view);
    CHECK(view->Guid == guid);

    GeodesicSimulationModule module;
    module.OnInit(test.World);
    module.OnTick(test.World, 1.0f / 60.0f);
    const auto& body = test.World.Registry().get<GeodesicBodyComponent>(camera);

    const auto readout = ComputeObserverReadout(test.World, kKerr, ObserverKind::Static, OrbitDirection::Prograde,
                                                view->PositionM, guid);
    CHECK(readout.Source == ObserverSource::Body);
    CHECK(readout.FreeFall);
    CHECK(readout.ClockRate == body.ClockRate);

    RaytraceParamsGpu params{};
    REQUIRE(FillObserverFromBody(params, test.World, guid));
    const auto velocity = ZamoVelocity(BlackHoleMetric(test.World.Registry().get<BlackHoleComponent>(test.World.FindByGuid(test.HoleGuid))),
                                       body.State);
    CHECK(params.ObserverVelocity.w == 1.0f);
    CHECK(params.ObserverVelocity.z == Approx(velocity[2]).epsilon(1e-6));

    // An entity without a Geodesic Body leaves the preset observer in charge
    CHECK_FALSE(FillObserverFromBody(params, test.World, test.HoleGuid));
    CHECK_FALSE(FillObserverFromBody(params, test.World, 0));

    // A body following another hole is not used for the view: the readout says so and the render takes the ZAMO
    test.World.Registry().get<GeodesicBodyComponent>(camera).BlackHole = 12345;
    REQUIRE(FillObserverFromBody(params, test.World, guid));
    CHECK(params.ObserverVelocity == glm::vec4(0.0f));
    CHECK(ComputeObserverReadout(test.World, kKerr, ObserverKind::Static, OrbitDirection::Prograde, view->PositionM, guid).Source ==
          ObserverSource::Fallback);
}

TEST_CASE("Long durations are shown in readable units", "[simulation][observer][readout]")
{
    CHECK(FormatLongDuration(30.0) == "30 s");
    CHECK(FormatLongDuration(3600.0 * 10.8) == "10.8 h");
    CHECK(FormatLongDuration(3600.0 * 61888.0) == "7.06 years");
    CHECK(FormatLongDuration(std::numeric_limits<double>::infinity()) == "forever");
}
