#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

namespace
{
    constexpr double kMuSun = 132712440041.279419e9;   // m^3/s^2, DE440
    constexpr double kMuEarth = 398600.435507e9;
    constexpr double kMuJupiter = 126686531.9e9;
    constexpr double kAuM = 149597870700.0;
    constexpr double kTwoPi = 2.0 * std::numbers::pi;

    // Orbit about `mu` from perihelion: radius, speed, in the x-y plane
    void AddKeplerOrbit(NBodySystem& system, const double mu, const double semiMajorM, const double eccentricity,
                        const double tilt = 0.0, const double testMu = 0.0)
    {
        const double rp = semiMajorM * (1.0 - eccentricity);
        const double vp = std::sqrt(mu * (1.0 + eccentricity) / rp);
        const glm::dvec3 position{rp, 0.0, 0.0};
        const glm::dvec3 velocity{0.0, vp * std::cos(tilt), vp * std::sin(tilt)};
        system.Add(position, velocity, testMu);
    }

    double Period(const double mu, const double a) { return kTwoPi * std::sqrt(a * a * a / mu); }

    // Laplace-Runge-Lenz angle of the orbit of body `i` about body 0 in the x-y plane
    double PerihelionAngle(const NBodySystem& s, const std::size_t i, const double mu)
    {
        const glm::dvec3 r = s.Position[i] - s.Position[0];
        const glm::dvec3 v = s.Velocity[i] - s.Velocity[0];
        const glm::dvec3 e = glm::cross(v, glm::cross(r, v)) / mu - r / glm::length(r);
        return std::atan2(e.y, e.x);
    }
}

TEST_CASE("The N-body integrator closes a Kepler orbit and converges at sixth order", "[simulation][nbody]")
{
    const double a = kAuM, e = 0.2;
    const double period = Period(kMuSun, a);
    const auto closureError = [&](const double stepS)
    {
        NBodySystem system;
        system.Add({0, 0, 0}, {0, 0, 0}, kMuSun);
        AddKeplerOrbit(system, kMuSun, a, e);
        const glm::dvec3 start = system.Position[1];
        NBodyWorkspace work;
        NBodyAdvance(system, period, stepS, false, work);
        return glm::length(system.Position[1] - start);
    };

    const double coarse = closureError(period / 100.0);
    const double fine = closureError(period / 200.0);
    CHECK(fine < 1e-5 * kAuM); // a few thousand metres after one year at 200 steps
    // Halving the step divides the error by 2^6 = 64 (the closure error is dominated by the phase error)
    CHECK(coarse / fine == Approx(64.0).epsilon(0.25));
}

TEST_CASE("Energy, momentum and angular momentum are conserved by the Newtonian system", "[simulation][nbody]")
{
    NBodySystem system;
    system.Add({0, 0, 0}, {0, 0, 0}, kMuSun);
    AddKeplerOrbit(system, kMuSun, 1.0 * kAuM, 0.0167, 0.0, kMuEarth);
    AddKeplerOrbit(system, kMuSun, 5.2 * kAuM, 0.0489, 0.02, kMuJupiter);
    // The Moon: Earth's orbit speed plus its own about the Earth
    const glm::dvec3 moonOffset{3.844e8, 0, 0};
    system.Add(system.Position[1] + moonOffset, system.Velocity[1] + glm::dvec3(0, 1022.0, 0), 4902.8e9);

    const double e0 = NBodyEnergy(system);
    const glm::dvec3 p0 = NBodyMomentum(system);
    const double shortest = NBodyDynamicalTime(system);
    NBodyWorkspace work;
    NBodyAdvance(system, 10.0 * 365.25 * 86400.0, 0.05 * shortest, false, work);

    CHECK(std::abs(NBodyEnergy(system) / e0 - 1.0) < 1e-10);
    const glm::dvec3 p1 = NBodyMomentum(system);
    CHECK(glm::length(p1 - p0) / (kMuEarth * 3e4) < 1e-10);
}

TEST_CASE("1PN terms advance Mercury's perihelion by 6 pi GM / (c^2 a (1 - e^2)) per orbit", "[simulation][nbody][relativity]")
{
    const double a = 0.38709927 * kAuM, e = 0.20563593;
    const double mu = kMuSun + 22031.868551e9;
    const double expectedPerOrbit = 6.0 * std::numbers::pi * mu
                                    / (Units::kSpeedOfLight * Units::kSpeedOfLight * a * (1.0 - e * e));
    CHECK(expectedPerOrbit * 180.0 / std::numbers::pi * 3600.0 * 415.2 == Approx(42.98).epsilon(0.01));  // arcsec per century

    const auto advance = [&](const bool relativistic)
    {
        NBodySystem system;
        system.Add({0, 0, 0}, {0, 0, 0}, kMuSun);
        AddKeplerOrbit(system, kMuSun, a, e, 0.0, 22031.868551e9);
        const double period = Period(mu, a);
        NBodyWorkspace work;
        const double before = PerihelionAngle(system, 1, mu);
        NBodyAdvance(system, 200.0 * period, period / 400.0, relativistic, work);
        // Back at the same Kepler phase only on average; compare the vectors at integer periods
        return (PerihelionAngle(system, 1, mu) - before) / 200.0;
    };

    CHECK(advance(true) == Approx(expectedPerOrbit).epsilon(0.03));
    CHECK(std::abs(advance(false)) < 0.01 * expectedPerOrbit);
}

TEST_CASE("J2 turns the orbital plane of a satellite at the nodal precession rate", "[simulation][nbody]")
{
    constexpr double j2 = 1.08262668e-3, radiusM = 6378.137e3;
    const double a = 7.0e6, inclination = 60.0 * std::numbers::pi / 180.0;
    NBodySystem system;
    system.Add({0, 0, 0}, {0, 0, 0}, kMuEarth, j2, radiusM, {0, 0, 1});
    const double v = std::sqrt(kMuEarth / a);
    system.Add({a, 0, 0}, {0, v * std::cos(inclination), v * std::sin(inclination)}, 0.0);

    const auto node = [&]
    {
        const glm::dvec3 h = glm::cross(system.Position[1], system.Velocity[1]);
        return std::atan2(h.x, -h.y);
    };
    const double start = node();
    const double period = Period(kMuEarth, a);
    NBodyWorkspace work;
    const double span = 20.0 * period;
    NBodyAdvance(system, span, period / 200.0, false, work);

    const double meanMotion = kTwoPi / period;
    const double expectedRate = -1.5 * meanMotion * j2 * (radiusM / a) * (radiusM / a) * std::cos(inclination);
    const double measured = (node() - start) / span;
    CHECK(measured == Approx(expectedRate).epsilon(0.03));
    CHECK(measured < 0.0); // a prograde orbit regresses
}

TEST_CASE("An oblate body takes the reaction of its J2 pull", "[simulation][nbody]")
{
    NBodySystem system;
    system.Add({0, 0, 0}, {0, 0, 0}, kMuEarth, 1.08262668e-3, 6378.137e3, {0, 0, 1});
    system.Add({7.0e6, 0, 3.0e6}, {0, 7000.0, 0}, 4902.8e9);
    NBodyWorkspace work;
    NBodyAcceleration(system, false, work);
    const glm::dvec3 momentumRate = kMuEarth * work.Acceleration[0] + 4902.8e9 * work.Acceleration[1];
    CHECK(glm::length(momentumRate) < 1e-12 * 4902.8e9 * glm::length(work.Acceleration[1]));
}

TEST_CASE("NBodySimulationModule moves bodies by the scene time scale and writes them back", "[simulation][nbody]")
{
    RegisterComponents();
    Scene scene("NBodyTest");
    constexpr double yearS = 365.25 * 86400.0;
    scene.SetTimeScale(86400.0);

    const auto sun = scene.CreateEntity("Sun");
    scene.Registry().emplace<TransformComponent>(sun, TransformComponent{.Position = {0, 0, 0}});
    scene.Registry().emplace<NBodyComponent>(sun, NBodyComponent{.GM = kMuSun});
    const auto planet = scene.CreateEntity("Planet");
    const double a = kAuM, v = std::sqrt(kMuSun / a);
    scene.Registry().emplace<TransformComponent>(planet, TransformComponent{.Position = {a, 0, 0}});
    scene.Registry().emplace<NBodyComponent>(planet, NBodyComponent{.Velocity = {0, 0, -v}});

    NBodySimulationModule module;
    module.SetRelativistic(false);
    constexpr float dt = 1.0f / 60.0f;
    const double period = Period(kMuSun, a);
    const int ticks = static_cast<int>(std::round(period / (dt * 86400.0)));
    for (int i = 0; i < ticks; ++i) module.OnTick(scene, dt);
    CHECK(module.LastSteps() >= 1);

    const auto position = scene.Registry().get<TransformComponent>(planet).Position;
    CHECK(glm::length(position) == Approx(a).epsilon(1e-6));
    // One orbit (to within the rounding of the tick count, 1440 s of 3.1e7)
    CHECK(glm::length(position - glm::dvec3(a, 0, 0)) < 2e-4 * a);
    CHECK(scene.Registry().get<NBodyComponent>(planet).Velocity != glm::dvec3(0, 0, -v));
    CHECK(yearS > period * 0.99);
}

TEST_CASE("A negative rotation period turns a Surface the other way", "[simulation][nbody][bodies]")
{
    CHECK(AdvanceRotation(0.0, 6.0, -24.0) == Approx(1.5 * std::numbers::pi)); // wraps to 3 pi / 2
    CHECK(AdvanceRotation(1.0, 5.0, 0.0) == 1.0);
}
