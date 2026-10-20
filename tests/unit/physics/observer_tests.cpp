#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import glm;
import MoleHole;
import std;

using namespace MoleHole;
using Catch::Approx;

namespace
{
    constexpr double kHalfPi = std::numbers::pi / 2.0;

    void CheckOrthonormal(const KerrNewmanParams& p, const double r, const double theta, const Tetrad& tetrad)
    {
        const auto g = MetricTensor(p, r, theta);
        for (std::size_t a = 0; a < 4; ++a)
        {
            for (std::size_t b = 0; b < 4; ++b)
            {
                const double expected = a != b ? 0.0 : (a == 0 ? -1.0 : 1.0);
                INFO("e_" << a << " . e_" << b);
                CHECK(MetricDot(g, tetrad[a], tetrad[b]) == Approx(expected).margin(1e-11));
            }
        }
    }

    // Photon momentum ε (e_0 + k^i e_i) built in `tetrad`, as measured by the ZAMO at the same point.
    BoostedPhoton MeasureInZamo(const KerrNewmanParams& p, const double r, const double theta, const Tetrad& tetrad,
                                const Vector3& direction)
    {
        Vector4 momentum{};
        for (std::size_t m = 0; m < 4; ++m)
        {
            momentum[m] = tetrad[0][m] + direction[0] * tetrad[1][m] + direction[1] * tetrad[2][m]
                          + direction[2] * tetrad[3][m];
        }
        const auto g = MetricTensor(p, r, theta);
        const auto zamo = ZamoTetrad(p, r, theta);
        const double energy = -MetricDot(g, momentum, zamo[0]);
        return {{MetricDot(g, momentum, zamo[1]) / energy, MetricDot(g, momentum, zamo[2]) / energy,
                 MetricDot(g, momentum, zamo[3]) / energy},
                energy};
    }
}

TEST_CASE("Observer tetrads are orthonormal", "[physics][observer]")
{
    const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.9, .Charge = 0.3};
    for (const auto [r, theta] : {std::pair{6.0, kHalfPi}, std::pair{3.0, 0.7}, std::pair{25.0, 2.9}})
    {
        INFO("r = " << r << ", theta = " << theta);
        CheckOrthonormal(p, r, theta, ZamoTetrad(p, r, theta));
        const Vector3 velocity{0.3, -0.4, 0.5};
        const auto boosted = ObserverTetrad(p, r, theta, velocity);
        CheckOrthonormal(p, r, theta, boosted);
        CheckOrthonormal(p, r, theta, GramSchmidtTetrad(p, r, theta, boosted[0]));
        // Gram-Schmidt keeps the 4-velocity
        const auto schmidt = GramSchmidtTetrad(p, r, theta, FourVelocity(p, r, theta, velocity));
        for (std::size_t m = 0; m < 4; ++m) CHECK(schmidt[0][m] == Approx(boosted[0][m]).margin(1e-12));
    }
}

TEST_CASE("Orbiting observers in the equatorial plane move on circular geodesics", "[physics][observer]")
{
    for (const auto direction : {OrbitDirection::Prograde, OrbitDirection::Retrograde})
    {
        const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.7};
        const double r = 9.0;
        const auto velocity = ObserverVelocity(p, ObserverKind::Orbiting, r, kHalfPi, direction);
        REQUIRE(velocity);
        const auto u = FourVelocity(p, r, kHalfPi, *velocity);
        const double omega = OrbitalFrequency(p, r, direction);
        const double ut = 1.0 / std::sqrt(CircularOrbitNorm(p, r, direction));
        CHECK(u[0] == Approx(ut).epsilon(1e-12));
        CHECK(u[1] == Approx(0.0).margin(1e-14));
        CHECK(u[2] == Approx(0.0).margin(1e-14));
        CHECK(u[3] == Approx(ut * omega).epsilon(1e-12));

        // Same as the geodesic integrator's circular orbit
        const auto orbit = MakeCircularOrbit<double>(p, r, direction);
        REQUIRE(orbit);
        const auto fromGeodesic = ZamoVelocity(p, *orbit);
        for (std::size_t i = 0; i < 3; ++i) CHECK(fromGeodesic[i] == Approx((*velocity)[i]).margin(1e-12));
    }
}

TEST_CASE("Clock rates match the closed forms", "[physics][observer]")
{
    // Schwarzschild static observer: sqrt(1 - 2M/r)
    const KerrNewmanParams schwarzschild{.Mass = 1.0};
    for (const double r : {3.0, 10.0, 100.0})
    {
        const auto still = ObserverVelocity(schwarzschild, ObserverKind::Static, r, 1.0);
        REQUIRE(still);
        CHECK(ProperTimeRate(schwarzschild, r, 1.0, *still) == Approx(std::sqrt(1.0 - 2.0 / r)));
    }

    // Kerr circular orbit (Bardeen, Press & Teukolsky 1972): u^t = (r^{3/2} ± a) / (r^{3/4} sqrt(r^{3/2} - 3 r^{1/2} ± 2a))
    for (const double a : {0.0, 0.5, 0.9})
    {
        const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = a};
        for (const double r : {7.0, 12.0})
        {
            for (const double sign : {1.0, -1.0})
            {
                const auto direction = sign > 0.0 ? OrbitDirection::Prograde : OrbitDirection::Retrograde;
                const auto velocity = ObserverVelocity(p, ObserverKind::Orbiting, r, kHalfPi, direction);
                REQUIRE(velocity);
                const double ut = (std::pow(r, 1.5) + sign * a)
                                  / (std::pow(r, 0.75) * std::sqrt(std::pow(r, 1.5) - 3.0 * std::sqrt(r) + sign * 2.0 * a));
                CHECK(ProperTimeRate(p, r, kHalfPi, *velocity) == Approx(1.0 / ut).epsilon(1e-12));
            }
        }
    }

    // The ZAMO's clock runs at the lapse
    const KerrNewmanParams kerr{.Mass = 1.0, .SpinParameter = 0.9};
    CHECK(ProperTimeRate(kerr, 4.0, 1.0, {0.0, 0.0, 0.0}) == Approx(EvaluateZamoFrame(kerr, 4.0, 1.0).Alpha));
}

TEST_CASE("Static observers cannot exist inside the ergosphere", "[physics][observer]")
{
    const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.9};
    const double theta = 1.2;
    const double ergosphere = ErgosphereRadius(p, theta);
    CHECK_FALSE(ObserverVelocity(p, ObserverKind::Static, 0.99 * ergosphere, theta));
    const auto outside = ObserverVelocity(p, ObserverKind::Static, 1.0001 * ergosphere, theta);
    REQUIRE(outside);
    // Moving backwards against the frame dragging, close to light speed at the static limit
    CHECK((*outside)[2] < -0.95);
    CHECK(ProperTimeRate(p, 1.0001 * ergosphere, theta, *outside) < 0.05);

    // The ZAMO exists down to the horizon; nothing exists at or inside it
    CHECK(ObserverVelocity(p, ObserverKind::Zamo, 0.99 * ergosphere, theta));
    CHECK_FALSE(ObserverVelocity(p, ObserverKind::Zamo, OuterHorizonRadius(p), theta));
    // Orbiting needs a timelike circular orbit
    CHECK_FALSE(ObserverVelocity(p, ObserverKind::Orbiting, 0.5 * (OuterHorizonRadius(p) + PhotonOrbitRadius(p, OrbitDirection::Prograde)),
                                 kHalfPi));
}

TEST_CASE("Raindrops fall at the Newtonian escape speed and keep their latitude", "[physics][observer]")
{
    // Schwarzschild: |v| = sqrt(2M/r) relative to the static observer (= ZAMO), straight inwards
    const KerrNewmanParams schwarzschild{.Mass = 1.0};
    for (const double r : {2.5, 6.0, 50.0})
    {
        const auto velocity = ObserverVelocity(schwarzschild, ObserverKind::Raindrop, r, 1.0);
        REQUIRE(velocity);
        CHECK((*velocity)[0] == Approx(-std::sqrt(2.0 / r)).epsilon(1e-12));
        CHECK((*velocity)[1] == 0.0);
        CHECK((*velocity)[2] == 0.0);
    }

    // Kerr: still purely radial relative to the ZAMO (L = 0), and theta stays constant along the fall
    const KerrNewmanParams kerr{.Mass = 1.0, .SpinParameter = 0.9};
    const auto velocity = ObserverVelocity(kerr, ObserverKind::Raindrop, 8.0, 1.0);
    REQUIRE(velocity);
    CHECK((*velocity)[0] < 0.0);
    CHECK((*velocity)[2] == 0.0);

    auto state = MakeGeodesic<double>(kerr, GeodesicKind::Timelike, 8.0, 1.0, 0.0, 1.0, 0.0, 0.0, -1.0);
    REQUIRE(state);
    CHECK(CarterConstant(kerr, *state) == Approx(0.0).margin(1e-12));
    AdvanceGeodesic(kerr, *state, 100.0);
    CHECK(state->Status == GeodesicStatus::Captured);
    CHECK(state->Theta == Approx(1.0).epsilon(1e-9));
}

TEST_CASE("ZAMO velocity inverts the geodesic launch", "[physics][observer]")
{
    const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.8, .Charge = 0.2};
    const Vector3 velocity{-0.2, 0.15, 0.4};
    const auto state = MakeGeodesicFromZamoVelocity<double>(p, 7.0, 1.3, 0.5, velocity);
    REQUIRE(state);
    const auto recovered = ZamoVelocity(p, *state);
    for (std::size_t i = 0; i < 3; ++i) CHECK(recovered[i] == Approx(velocity[i]).epsilon(1e-12));
    // and the observer tetrad's 4-velocity is the geodesic's p^μ = g^{μν} p_ν
    const auto u = FourVelocity(p, 7.0, 1.3, velocity);
    const auto dy = GeodesicDerivative(p, *state);
    CHECK(u[0] == Approx(dy[5]).epsilon(1e-12));
    CHECK(u[1] == Approx(dy[0]).epsilon(1e-12));
    CHECK(u[2] == Approx(dy[1]).epsilon(1e-12));
    CHECK(u[3] == Approx(dy[4]).epsilon(1e-12));
}

TEST_CASE("Aberration and Doppler shift between frames", "[physics][observer]")
{
    const Vector3 velocity{0.0, 0.0, 0.6}; // γ = 1.25
    // Photon moving along the velocity: same direction, blueshifted by γ (1 + v) = 2 in the frame it is moving through
    const auto forward = BoostPhoton(velocity, {0.0, 0.0, 1.0});
    CHECK(forward.Direction[2] == Approx(1.0));
    CHECK(forward.EnergyRatio == Approx(2.0));

    // cos θ = (cos θ' + v) / (1 + v cos θ') and unit length, for a few directions
    for (const double cosine : {-0.9, -0.3, 0.0, 0.5})
    {
        const double sine = std::sqrt(1.0 - cosine * cosine);
        const auto boosted = BoostPhoton(velocity, {sine, 0.0, cosine});
        CHECK(Dot3(boosted.Direction, boosted.Direction) == Approx(1.0).epsilon(1e-14));
        CHECK(boosted.Direction[2] == Approx((cosine + 0.6) / (1.0 + 0.6 * cosine)).epsilon(1e-14));
        CHECK(boosted.EnergyRatio == Approx(1.25 * (1.0 + 0.6 * cosine)));
        // Boosting back recovers the photon
        const auto back = BoostPhoton({0.0, 0.0, -0.6}, boosted.Direction);
        CHECK(back.Direction[0] == Approx(sine).margin(1e-14));
        CHECK(back.EnergyRatio * boosted.EnergyRatio == Approx(1.0));
    }

    // James, von Tunzelmann, Franklin & Thorne (2015), eq. A.9: camera moving with speed β along e_y, N the
    // direction towards the source in the camera frame, n_F the propagation direction seen by the FIDO (= ZAMO)
    const double beta = 0.546;
    const Vector3 n{0.3, -0.5, std::sqrt(1.0 - 0.34)};
    const auto fido = BoostPhoton({0.0, beta, 0.0}, {-n[0], -n[1], -n[2]});
    CHECK(fido.Direction[1] == Approx((-n[1] + beta) / (1.0 - beta * n[1])).epsilon(1e-14));
    CHECK(fido.Direction[0] == Approx(-std::sqrt(1.0 - beta * beta) * n[0] / (1.0 - beta * n[1])).epsilon(1e-14));
    CHECK(fido.Direction[2] == Approx(-std::sqrt(1.0 - beta * beta) * n[2] / (1.0 - beta * n[1])).epsilon(1e-14));
}

TEST_CASE("The photon boost agrees with the boosted tetrad", "[physics][observer]")
{
    const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.9};
    const double r = 5.0;
    const double theta = 1.1;
    const Vector3 velocity{-0.3, 0.2, 0.5};
    const auto tetrad = ObserverTetrad(p, r, theta, velocity);
    for (const Vector3 direction : {Vector3{1.0, 0.0, 0.0}, Vector3{0.0, 0.6, -0.8}, Vector3{-0.48, 0.6, 0.64}})
    {
        const auto measured = MeasureInZamo(p, r, theta, tetrad, direction);
        const auto boosted = BoostPhoton(velocity, direction);
        CHECK(measured.EnergyRatio == Approx(boosted.EnergyRatio).epsilon(1e-12));
        for (std::size_t i = 0; i < 3; ++i) CHECK(measured.Direction[i] == Approx(boosted.Direction[i]).margin(1e-12));
    }
}

TEST_CASE("FillObserverData uploads the camera's velocity relative to the ZAMO", "[rendering][observer]")
{
    RaytraceParamsGpu params{};
    params.NumBlackHoles = 1;
    params.MetricType = static_cast<std::int32_t>(MetricKind::Kerr);
    params.BlackHoles[0].Mass = 1.0f;
    params.BlackHoles[0].Spin = 0.5f;
    params.BlackHoles[0].SpinAxis = {0.0f, 1.0f, 0.0f};
    params.CameraPos = {10.0f, 0.0f, 0.0f}; // equatorial plane, r ≈ 10 M
    FillMetricData(params);

    FillObserverData(params, ObserverKind::Zamo, OrbitDirection::Prograde);
    CHECK(params.ObserverVelocity == glm::vec4(0.0f));

    FillObserverData(params, ObserverKind::Orbiting, OrbitDirection::Prograde);
    CHECK(params.ObserverVelocity.w == 1.0f);
    const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.5};
    const auto expected = ObserverVelocity(p, ObserverKind::Orbiting, BoyerLindquistRadiusTheta({10.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, 0.5).x,
                                           kHalfPi);
    REQUIRE(expected);
    CHECK(params.ObserverVelocity.z == Approx((*expected)[2]).epsilon(1e-6));
    CHECK(params.ObserverVelocity.x == Approx(0.0).margin(1e-7));

    // A static camera inside the ergosphere (r ≈ 1.885 M, between r+ ≈ 1.866 M and 2 M) falls back to the ZAMO
    params.CameraPos = {1.95f, 0.0f, 0.0f};
    FillObserverData(params, ObserverKind::Static, OrbitDirection::Prograde);
    CHECK(params.ObserverVelocity.w == 0.0f);
}
