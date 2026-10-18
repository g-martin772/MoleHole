#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import glm;
import MoleHole;
import std;

using namespace MoleHole;
using Catch::Approx;

namespace
{
    // Gargantua: a/M = 1 - δ with δ ≈ 1.3e-14 (Thorne 2014: "about one part in 100 trillion")
    constexpr double kGap = 1.3e-14;
    constexpr double kHalfPi = std::numbers::pi / 2.0;

    KerrNewmanParams Gargantua()
    {
        return BlackHoleMetric(BlackHoleComponent{.SpinRange = BlackHoleSpinRange::NearExtremal, .SpinGap = kGap});
    }
}

TEST_CASE("The spin range selects the Thorne limit or a near-extremal spin", "[physics][near-extremal]")
{
    CHECK(BlackHoleMetric(BlackHoleComponent{.Spin = 0.999}).SpinParameter == Approx(kMaxHorizonParameter));
    CHECK(BlackHoleMetric(BlackHoleComponent{.Spin = 0.5}).SpinParameter == Approx(0.5));
    CHECK(Gargantua().SpinParameter == 1.0 - kGap);
    CHECK(1.0 - Gargantua().SpinParameter == Approx(kGap).epsilon(1e-2)); // double spacing near 1 is 1.1e-16
    // Never closer to extremal than the minimum gap
    const auto limit = BlackHoleMetric(BlackHoleComponent{.SpinRange = BlackHoleSpinRange::NearExtremal, .SpinGap = 0.0});
    CHECK(limit.SpinParameter == kExtremalHorizonParameter);
    CHECK(OuterHorizonRadius(limit) > 1.0);
}

TEST_CASE("Near-extremal radii match the expansions of Bardeen, Press & Teukolsky", "[physics][near-extremal]")
{
    // BPT 1972 eq. 2.22 with a = M (1 - δ): r+ ≈ M[1 + (2δ)^{1/2}], r_ph ≈ M[1 + 2(2δ/3)^{1/2}] (from eq. 2.18 with
    // arccos(-â) ≈ π - (2δ)^{1/2}), r_mb ≈ M[1 + 2δ^{1/2}], r_ms ≈ M[1 + (4δ)^{1/3}]. Leading order, so the
    // tolerance follows the next term. δ is taken from the stored spin: 1 - 1.3e-14 is only representable to ~0.4 %.
    const auto p = Gargantua();
    const double delta = 1.0 - p.SpinParameter;
    CHECK(OuterHorizonRadius(p) - 1.0 == Approx(std::sqrt(2.0 * delta)).epsilon(1e-5));
    CHECK(PhotonOrbitRadius(p, OrbitDirection::Prograde) - 1.0 == Approx(2.0 * std::sqrt(2.0 * delta / 3.0)).epsilon(1e-5));
    CHECK(MarginallyBoundRadius(p, OrbitDirection::Prograde) - 1.0 == Approx(2.0 * std::sqrt(delta)).epsilon(1e-5));
    CHECK(ProgradeIscoRadius(p) - 1.0 == Approx(std::cbrt(4.0 * delta)).epsilon(1e-3));
    // Distinct and ordered, all outside the horizon
    CHECK(OuterHorizonRadius(p) < PhotonOrbitRadius(p, OrbitDirection::Prograde));
    CHECK(PhotonOrbitRadius(p, OrbitDirection::Prograde) < MarginallyBoundRadius(p, OrbitDirection::Prograde));
    CHECK(MarginallyBoundRadius(p, OrbitDirection::Prograde) < ProgradeIscoRadius(p));
    // Retrograde orbits barely notice: ISCO 9 M at a = M (BPT, text after eq. 2.21)
    CHECK(IscoRadius(p, OrbitDirection::Retrograde) == Approx(9.0).epsilon(1e-6));
}

TEST_CASE("Gargantua slows clocks at the ISCO to Miller's planet's 1 hour : 7 years", "[physics][near-extremal]")
{
    const auto p = Gargantua();
    const double isco = ProgradeIscoRadius(p);
    const double slowdown = 1.0 / std::sqrt(CircularOrbitNorm(p, isco, OrbitDirection::Prograde));
    const double yearsPerHour = slowdown / (365.25 * 24.0);
    CHECK(yearsPerHour > 6.9);
    CHECK(yearsPerHour < 7.2);

    // The same rate from the observer machinery: α/γ of the orbiting observer
    const auto velocity = ObserverVelocity(p, ObserverKind::Orbiting, isco, kHalfPi);
    REQUIRE(velocity);
    CHECK(1.0 / ProperTimeRate(p, isco, kHalfPi, *velocity) == Approx(slowdown).epsilon(1e-6));
    // ...and the ISCO gas moves at c/2 relative to the ZAMO (BPT 1972, §III)
    CHECK(std::abs((*velocity)[2]) == Approx(0.5).epsilon(1e-3));

    // With the default Thorne limit the same orbit gives only about 11 x
    const auto thorne = BlackHoleMetric(BlackHoleComponent{.Spin = 0.998});
    CHECK(1.0 / std::sqrt(CircularOrbitNorm(thorne, ProgradeIscoRadius(thorne), OrbitDirection::Prograde)) ==
          Approx(10.8).epsilon(0.01));
}

TEST_CASE("The Kerr closed forms agree with the general Kerr-Newman path", "[physics][near-extremal]")
{
    // A negligible charge switches to the numeric path through the equatorial metric
    const KerrNewmanParams kerr{.Mass = 1.0, .SpinParameter = 0.9};
    const KerrNewmanParams general{.Mass = 1.0, .SpinParameter = 0.9, .Charge = 1e-9};
    REQUIRE(UsesKerrClosedForms(kerr));
    REQUIRE_FALSE(UsesKerrClosedForms(general));
    for (const auto direction : {OrbitDirection::Prograde, OrbitDirection::Retrograde})
    {
        for (const double r : {5.0, 7.0, 20.0})
        {
            INFO("r = " << r);
            CHECK(OrbitalFrequency(kerr, r, direction) == Approx(OrbitalFrequency(general, r, direction)).epsilon(1e-9));
            CHECK(CircularOrbitEnergy(kerr, r, direction) == Approx(CircularOrbitEnergy(general, r, direction)).epsilon(1e-9));
            CHECK(CircularOrbitAngularMomentum(kerr, r, direction) ==
                  Approx(CircularOrbitAngularMomentum(general, r, direction)).epsilon(1e-9));
            CHECK(CircularOrbitNorm(kerr, r, direction) == Approx(CircularOrbitNorm(general, r, direction)).epsilon(1e-9));
        }
        CHECK(IscoRadius(kerr, direction) == Approx(IscoRadius(general, direction)).epsilon(1e-6));
        CHECK(PhotonOrbitRadius(kerr, direction) == Approx(PhotonOrbitRadius(general, direction)).epsilon(1e-9));
    }
}

TEST_CASE("Circular orbits deep in Gargantua's throat stay circular", "[physics][near-extremal][geodesic]")
{
    const auto p = Gargantua();
    const double r = 1.0001; // just outside the ISCO at 1.0000373 M
    auto state = MakeCircularOrbit<double>(p, r, OrbitDirection::Prograde);
    REQUIRE(state);
    CHECK(MassShellResidual(p, *state) == Approx(0.0).margin(1e-9));

    const double omega = OrbitalFrequency(p, r, OrbitDirection::Prograde);
    const double duration = 5.0 * 2.0 * std::numbers::pi / omega;
    AdvanceToCoordinateTime(p, *state, duration);
    REQUIRE(state->Status == GeodesicStatus::Ok);
    // Deviation measured against the distance to r = M, where everything here is crowded together
    CHECK(std::abs(state->R - r) / (r - 1.0) < 1e-6);
    CHECK(state->Phi == Approx(omega * duration).epsilon(1e-8));
    CHECK(state->Lambda / state->T == Approx(std::sqrt(CircularOrbitNorm(p, r, OrbitDirection::Prograde))).epsilon(1e-6));
}

TEST_CASE("A near-extremal hole reaches the renderer without NaNs", "[physics][near-extremal][rendering]")
{
    GPP::Scene scene("Gargantua");
    const auto entity = scene.CreateEntity("Gargantua");
    scene.Registry().emplace<GPP::TransformComponent>(entity);
    scene.Registry().emplace<BlackHoleComponent>(
        entity, BlackHoleComponent{.Mass = 1e8 * GPP::Units::kSolarMass, .SpinRange = BlackHoleSpinRange::NearExtremal,
                                   .SpinGap = kGap});
    RaytraceParamsGpu params{};
    params.MetricType = static_cast<std::int32_t>(MetricKind::Kerr);
    params.CameraPos = {20.0f, 0.0f, 0.0f};
    RegisterComponents();
    const auto frame = RenderFrame::ForScene(scene, FrameMode::PrimaryBlackHole, {});
    FillSceneData(params, scene, frame);
    FillMetricData(params);
    FillObserverData(params, ObserverKind::Orbiting, OrbitDirection::Prograde);

    const auto& hole = params.BlackHoles[0];
    CHECK(hole.Spin == Approx(1.0f));
    CHECK(std::isfinite(hole.HorizonRadius));
    CHECK(hole.HorizonRadius / hole.Mass == Approx(1.0f).margin(1e-5f));
    CHECK(hole.IscoRadius / hole.Mass == Approx(1.0f).margin(1e-3f));
    CHECK(std::isfinite(params.DiskInfo[0].x));
    CHECK(params.ObserverVelocity.w == 1.0f);
}
