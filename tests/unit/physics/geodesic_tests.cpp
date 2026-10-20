#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import std;

using namespace MoleHole;
using Catch::Approx;

namespace
{
    constexpr double kHalfPi = std::numbers::pi / 2.0;

    // ½ g^{μν} p_μ p_ν from the explicit Boyer–Lindquist inverse metric of Kerr–Newman, independent of Carter's form.
    double InverseMetricHamiltonian(const KerrNewmanParams& p, const double r, const double theta, const double pr,
                                    const double pTheta, const double energy, const double angularMomentum)
    {
        const double a = p.SpinParameter;
        const double s = std::sin(theta);
        const double c = std::cos(theta);
        const double sigma = r * r + a * a * c * c;
        const double delta = r * r - 2.0 * p.Mass * r + a * a + p.Charge * p.Charge;
        const double big = (r * r + a * a) * (r * r + a * a) - delta * a * a * s * s;
        const double gtt = -big / (sigma * delta);
        const double gtphi = -a * (2.0 * p.Mass * r - p.Charge * p.Charge) / (sigma * delta);
        const double gphiphi = (delta - a * a * s * s) / (sigma * delta * s * s);
        const double pt = -energy;
        return 0.5 * (gtt * pt * pt + 2.0 * gtphi * pt * angularMomentum + gphiphi * angularMomentum * angularMomentum
                      + delta / sigma * pr * pr + pTheta * pTheta / sigma);
    }

    // Steps a geodesic in affine chunks until p_r changes sign from negative to positive (a periapsis passage),
    // then homes in on p_r = 0 with Newton steps on dp_r/dλ.
    void AdvanceToPeriapsis(const KerrNewmanParams& p, GeodesicState<double>& state, const double chunk,
                            const GeodesicTolerance& tolerance)
    {
        bool falling = state.Pr < 0.0;
        for (int i = 0; i < 1'000'000 && state.Status == GeodesicStatus::Ok; ++i)
        {
            const auto before = state;
            AdvanceGeodesic(p, state, chunk, tolerance);
            if (falling && state.Pr >= 0.0)
            {
                state = before;
                break;
            }
            if (state.Pr < 0.0) falling = true;
        }
        for (int i = 0; i < 20; ++i)
        {
            const double dpr = GeodesicDerivative(p, state)[2];
            const double step = -state.Pr / dpr;
            if (std::abs(step) < 1e-13) break;
            AdvanceGeodesic(p, state, step, tolerance);
        }
    }
}

TEST_CASE("Carter's Hamiltonian equals the inverse-metric contraction and generates the motion", "[physics][geodesic]")
{
    const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.7, .Charge = 0.3};
    GeodesicState<double> state{.R = 7.3, .Theta = 1.1, .Phi = 0.4, .Pr = 0.21, .Ptheta = -1.7, .Energy = 0.95,
                                .AngularMomentum = 3.2};
    const double r = state.R;
    const double th = state.Theta;
    const double pr = state.Pr;
    const double pth = state.Ptheta;
    const double e = state.Energy;
    const double l = state.AngularMomentum;

    CHECK(GeodesicHamiltonian(p, state) == Approx(InverseMetricHamiltonian(p, r, th, pr, pth, e, l)).epsilon(1e-12));

    // Hamilton's equations by central differences: dx/dλ = ∂H/∂p, dp/dλ = -∂H/∂x, dt/dλ = -∂H/∂E, dφ/dλ = ∂H/∂L.
    const auto h = [&](const double dr, const double dth, const double dpr, const double dpth, const double de,
                       const double dl)
    { return InverseMetricHamiltonian(p, r + dr, th + dth, pr + dpr, pth + dpth, e + de, l + dl); };
    constexpr double kEps = 1e-6;
    const auto dy = GeodesicDerivative(p, state);
    CHECK(dy[0] == Approx((h(0, 0, kEps, 0, 0, 0) - h(0, 0, -kEps, 0, 0, 0)) / (2 * kEps)).epsilon(1e-7));
    CHECK(dy[1] == Approx((h(0, 0, 0, kEps, 0, 0) - h(0, 0, 0, -kEps, 0, 0)) / (2 * kEps)).epsilon(1e-7));
    CHECK(dy[2] == Approx(-(h(kEps, 0, 0, 0, 0, 0) - h(-kEps, 0, 0, 0, 0, 0)) / (2 * kEps)).epsilon(1e-6));
    CHECK(dy[3] == Approx(-(h(0, kEps, 0, 0, 0, 0) - h(0, -kEps, 0, 0, 0, 0)) / (2 * kEps)).epsilon(1e-6));
    CHECK(dy[4] == Approx((h(0, 0, 0, 0, 0, kEps) - h(0, 0, 0, 0, 0, -kEps)) / (2 * kEps)).epsilon(1e-7));
    CHECK(dy[5] == Approx(-(h(0, 0, 0, 0, kEps, 0) - h(0, 0, 0, 0, -kEps, 0)) / (2 * kEps)).epsilon(1e-7));
}

TEST_CASE("Initial conditions lie on the mass shell", "[physics][geodesic]")
{
    const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.9};

    const auto particle = MakeGeodesicFromZamoVelocity<double>(p, 8.0, 1.2, 0.0, {0.1, -0.2, 0.3});
    REQUIRE(particle);
    CHECK(MassShellResidual(p, *particle) == Approx(0.0).margin(1e-12));

    const auto photon = MakePhotonFromZamoDirection<double>(p, 8.0, 1.2, 0.0, {1.0, 2.0, -0.5});
    REQUIRE(photon);
    CHECK(photon->Kind == GeodesicKind::Null);
    CHECK(MassShellResidual(p, *photon) == Approx(0.0).margin(1e-12));

    for (const auto direction : {OrbitDirection::Prograde, OrbitDirection::Retrograde})
    {
        const auto orbit = MakeCircularOrbit<double>(p, 10.0, direction);
        REQUIRE(orbit);
        CHECK(MassShellResidual(p, *orbit) == Approx(0.0).margin(1e-12));
    }

    CHECK_FALSE(MakeGeodesicFromZamoVelocity<double>(p, 8.0, 1.2, 0.0, {0.8, 0.0, 0.7}));
    // Between the horizon and the photon orbit circular orbits are spacelike
    CHECK_FALSE(MakeCircularOrbit<double>(p, 0.5 * (OuterHorizonRadius(p) + PhotonOrbitRadius(p, OrbitDirection::Prograde)),
                                          OrbitDirection::Prograde));
    // Energy below the potential at r: no motion possible there
    CHECK_FALSE(MakeGeodesic<double>(p, GeodesicKind::Timelike, 10.0, kHalfPi, 0.0, 0.5, 3.0, 0.0, 1.0));
}

TEST_CASE("Circular orbits stay circular and keep their clock rate", "[physics][geodesic]")
{
    for (const double a : {0.0, 0.9, 0.998})
    {
        for (const auto direction : {OrbitDirection::Prograde, OrbitDirection::Retrograde})
        {
            const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = a};
            const double r = 1.2 * IscoRadius(p, direction);
            INFO("a = " << a << ", r = " << r << ", retrograde = " << (direction == OrbitDirection::Retrograde));
            auto state = MakeCircularOrbit<double>(p, r, direction);
            REQUIRE(state);

            const double omega = OrbitalFrequency(p, r, direction);
            const double duration = 20.0 * 2.0 * std::numbers::pi / std::abs(omega);
            AdvanceToCoordinateTime(p, *state, duration);

            REQUIRE(state->Status == GeodesicStatus::Ok);
            CHECK(state->T == Approx(duration).epsilon(1e-10));
            CHECK(state->R == Approx(r).epsilon(1e-8));
            CHECK(state->Theta == Approx(kHalfPi).epsilon(1e-10));
            CHECK(state->Phi == Approx(omega * duration).epsilon(1e-8));
            // dτ/dt = 1 / u^t = sqrt(-(g_tt + 2 g_tφ Ω + g_φφ Ω²))
            CHECK(state->Lambda / state->T == Approx(std::sqrt(CircularOrbitNorm(p, r, direction))).epsilon(1e-9));
        }
    }
}

TEST_CASE("An inclined eccentric Kerr orbit conserves the mass shell and Carter's constant", "[physics][geodesic]")
{
    const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.9, .Charge = 0.2};
    auto state = MakeGeodesicFromZamoVelocity<double>(p, 12.0, 1.2, 0.0, {0.05, 0.2, 0.2});
    REQUIRE(state);
    const double carter = CarterConstant(p, *state);
    REQUIRE(carter > 1.0);

    double rMin = state->R;
    double rMax = state->R;
    for (int i = 0; i < 100; ++i)
    {
        AdvanceGeodesic(p, *state, 100.0);
        rMin = std::min(rMin, state->R);
        rMax = std::max(rMax, state->R);
    }
    REQUIRE(state->Status == GeodesicStatus::Ok);
    CHECK(state->Lambda == Approx(1e4));
    CHECK(MassShellResidual(p, *state) == Approx(0.0).margin(1e-9));
    CHECK(CarterConstant(p, *state) == Approx(carter).epsilon(1e-8));
    // Bound and actually eccentric
    CHECK(rMin > IscoRadius(p, OrbitDirection::Prograde));
    CHECK(rMax < 100.0);
    CHECK(rMax - rMin > 1.0);
}

TEST_CASE("Schwarzschild periapsis advance matches the elliptic-integral result", "[physics][geodesic]")
{
    const KerrNewmanParams p{.Mass = 1.0};
    const GeodesicTolerance tolerance{.Relative = 1e-12, .Absolute = 1e-14};
    for (const auto [semiLatus, eccentricity] : {std::pair{20.0, 0.3}, std::pair{10.0, 0.5}})
    {
        INFO("p = " << semiLatus << ", e = " << eccentricity);
        const double denominator = semiLatus - 3.0 - eccentricity * eccentricity;
        const double energy = std::sqrt(((semiLatus - 2.0) * (semiLatus - 2.0) - 4.0 * eccentricity * eccentricity)
                                        / (semiLatus * denominator));
        const double angularMomentum = semiLatus / std::sqrt(denominator);
        const double periapsis = semiLatus / (1.0 + eccentricity);

        auto state = MakeGeodesic<double>(p, GeodesicKind::Timelike, periapsis, kHalfPi, 0.0, energy, angularMomentum,
                                          0.0, 1.0);
        REQUIRE(state);
        CHECK(state->Pr == 0.0); // periapsis is a turning point
        AdvanceToPeriapsis(p, *state, 10.0, tolerance);
        REQUIRE(state->Status == GeodesicStatus::Ok);
        CHECK(state->R == Approx(periapsis).epsilon(1e-9));

        // Δφ per radial period = 4 sqrt(p / (p - 6 + 2e)) K(m), m = 4e / (p - 6 + 2e); std::comp_ellint_1 takes k = sqrt(m)
        const double q = semiLatus - 6.0 + 2.0 * eccentricity;
        const double expected = 4.0 * std::sqrt(semiLatus / q) * std::comp_ellint_1(std::sqrt(4.0 * eccentricity / q));
        CHECK(state->Phi == Approx(expected).epsilon(1e-9));
        CHECK(state->Phi - 2.0 * std::numbers::pi > 0.0);
    }
}

TEST_CASE("A radial plunge from rest follows the cycloid in proper time", "[physics][geodesic]")
{
    const KerrNewmanParams p{.Mass = 1.0};
    constexpr double kStart = 10.0;
    auto state = MakeGeodesic<double>(p, GeodesicKind::Timelike, kStart, kHalfPi, 0.0, std::sqrt(1.0 - 2.0 / kStart),
                                      0.0, 0.0, -1.0);
    REQUIRE(state);
    AdvanceGeodesic(p, *state, 1e3);
    REQUIRE(state->Status == GeodesicStatus::Captured);
    CHECK(state->R < 2.0 + 1e-3);

    // r = r0 / 2 (1 + cos η), τ = sqrt(r0³ / 8M) (η + sin η)
    const double eta = std::acos(2.0 * state->R / kStart - 1.0);
    CHECK(state->Lambda == Approx(std::sqrt(kStart * kStart * kStart / 8.0) * (eta + std::sin(eta))).epsilon(1e-8));

    // A distant observer never sees it cross: coordinate time stalls at the horizon.
    auto distant = MakeGeodesic<double>(p, GeodesicKind::Timelike, kStart, kHalfPi, 0.0, std::sqrt(1.0 - 2.0 / kStart),
                                        0.0, 0.0, -1.0);
    REQUIRE(distant);
    AdvanceToCoordinateTime(p, *distant, 1e6);
    CHECK(distant->Status == GeodesicStatus::Captured);
    CHECK(distant->T < 1e3);
    CHECK(distant->T > state->Lambda);
}

TEST_CASE("The ISCO separates stable orbits from plunges", "[physics][geodesic]")
{
    const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.5};
    const double isco = ProgradeIscoRadius(p);
    const auto perturbed = [&](const double r)
    {
        // Slightly above the circular-orbit energy, heading inwards
        const double energy = CircularOrbitEnergy(p, r) * (1.0 + 1e-6);
        return MakeGeodesic<double>(p, GeodesicKind::Timelike, r, kHalfPi, 0.0, energy,
                                    CircularOrbitAngularMomentum(p, r), 0.0, -1.0);
    };

    auto outside = perturbed(1.05 * isco);
    REQUIRE(outside);
    AdvanceToCoordinateTime(p, *outside, 30.0 * 2.0 * std::numbers::pi / ProgradeOrbitalFrequency(p, 1.05 * isco));
    CHECK(outside->Status == GeodesicStatus::Ok);
    CHECK(outside->R > isco);

    auto inside = perturbed(0.95 * isco);
    REQUIRE(inside);
    AdvanceToCoordinateTime(p, *inside, 30.0 * 2.0 * std::numbers::pi / ProgradeOrbitalFrequency(p, 0.95 * isco));
    CHECK(inside->Status == GeodesicStatus::Captured);
}

TEST_CASE("Weak-field light deflection matches the post-Newtonian series", "[physics][geodesic]")
{
    const KerrNewmanParams p{.Mass = 1.0};
    const GeodesicTolerance tolerance{.Relative = 1e-12, .Absolute = 1e-14};
    constexpr double kImpact = 1000.0;
    constexpr double kStart = 1e7;
    auto state = MakeGeodesic<double>(p, GeodesicKind::Null, kStart, kHalfPi, 0.0, 1.0, kImpact, 0.0, -1.0);
    REQUIRE(state);

    // Roughly out to the starting radius again; far from the hole the affine parameter is the distance travelled
    AdvanceGeodesic(p, *state, 2.0 * kStart, tolerance);
    REQUIRE(state->Status == GeodesicStatus::Ok);
    REQUIRE(state->Pr > 0.0);

    // Deflection 4 (M/b) + 15π/4 (M/b)² + 128/3 (M/b)³ on top of the straight-line sweep between the two end radii
    const double x = 1.0 / kImpact;
    const double deflection = 4.0 * x + 15.0 * std::numbers::pi / 4.0 * x * x + 128.0 / 3.0 * x * x * x;
    const double sweep = std::numbers::pi - std::asin(kImpact / kStart) - std::asin(kImpact / state->R);
    // The bending still to come beyond r0 is about 2 M b / r0² ≈ 2e-11, the next series term (3465π/64)(M/b)⁴ ≈ 2e-10
    CHECK(state->Phi - sweep == Approx(deflection).epsilon(1e-6));
}

TEST_CASE("Light on the photon sphere stays there", "[physics][geodesic]")
{
    const KerrNewmanParams p{.Mass = 1.0};
    auto state = MakeGeodesic<double>(p, GeodesicKind::Null, 3.0, kHalfPi, 0.0, 1.0, std::sqrt(27.0), 0.0, 1.0);
    REQUIRE(state);
    CHECK(state->Pr == Approx(0.0).margin(1e-7));
    AdvanceToCoordinateTime(p, *state, 50.0);
    REQUIRE(state->Status == GeodesicStatus::Ok);
    CHECK(state->R == Approx(3.0).epsilon(1e-6));
    // Ω = 1 / sqrt(27) M
    CHECK(state->Phi == Approx(50.0 / std::sqrt(27.0)).epsilon(1e-8));
}

TEST_CASE("Fixed-step RK4 and adaptive Dormand-Prince agree", "[physics][geodesic]")
{
    const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.6};
    const auto start = MakeGeodesicFromZamoVelocity<double>(p, 15.0, 1.4, 0.0, {0.05, 0.1, 0.25});
    REQUIRE(start);

    auto fixed = *start;
    for (int i = 0; i < 20'000; ++i) StepGeodesicRk4(p, fixed, 0.01);
    auto adaptive = *start;
    AdvanceGeodesic(p, adaptive, 200.0);

    CHECK(fixed.Lambda == Approx(200.0));
    CHECK(adaptive.R == Approx(fixed.R).epsilon(1e-8));
    CHECK(adaptive.Theta == Approx(fixed.Theta).epsilon(1e-8));
    CHECK(adaptive.Phi == Approx(fixed.Phi).epsilon(1e-8));
    CHECK(adaptive.T == Approx(fixed.T).epsilon(1e-8));
}

TEST_CASE("Integration can be split into chunks and run backwards", "[physics][geodesic]")
{
    const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.9};
    const auto start = MakeGeodesicFromZamoVelocity<double>(p, 10.0, 1.0, 0.0, {0.1, 0.1, 0.3});
    REQUIRE(start);

    auto once = *start;
    AdvanceGeodesic(p, once, 300.0);
    auto chunked = *start;
    for (int i = 0; i < 30; ++i) AdvanceGeodesic(p, chunked, 10.0);
    CHECK(chunked.R == Approx(once.R).epsilon(1e-8));
    CHECK(chunked.Phi == Approx(once.Phi).epsilon(1e-8));

    AdvanceGeodesic(p, chunked, -300.0);
    CHECK(chunked.R == Approx(start->R).epsilon(1e-8));
    CHECK(chunked.Theta == Approx(start->Theta).epsilon(1e-8));
    CHECK(chunked.Phi == Approx(start->Phi).margin(1e-8));
    CHECK(chunked.T == Approx(0.0).margin(1e-7));
}

TEST_CASE("Quad precision tightens the conservation laws", "[physics][geodesic]")
{
    if constexpr (GPP::Units::kHasFloat128)
    {
        using Wide = GPP::Units::Wide;
        const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.9};
        auto state = MakeGeodesicFromZamoVelocity<Wide>(p, 12.0, 1.2, 0.0, {0.05, 0.2, 0.2});
        REQUIRE(state);
        const Wide carter = CarterConstant(p, *state);
        AdvanceGeodesic(p, *state, Wide(300), {.Relative = 1e-20, .Absolute = 1e-24});
        REQUIRE(state->Status == GeodesicStatus::Ok);
        CHECK(static_cast<double>(std::abs(MassShellResidual(p, *state))) < 1e-18);
        CHECK(static_cast<double>(std::abs(CarterConstant(p, *state) - carter) / carter) < 1e-17);
    }
}
