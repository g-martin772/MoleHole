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

    using Metric = std::array<Vector4, 4>;

    // Inverse of a Boyer–Lindquist metric: diagonal in r and θ, a 2×2 block in (t, φ).
    Metric InverseMetric(const Metric& g)
    {
        Metric inverse{};
        const double det = g[0][0] * g[3][3] - g[0][3] * g[0][3];
        inverse[0][0] = g[3][3] / det;
        inverse[3][3] = g[0][0] / det;
        inverse[0][3] = inverse[3][0] = -g[0][3] / det;
        inverse[1][1] = 1.0 / g[1][1];
        inverse[2][2] = 1.0 / g[2][2];
        return inverse;
    }

    // Covariant Riemann tensor R_abcd from central differences of the coordinate metric, the independent reference for
    // the closed form in Carter's frame:
    //   R_abcd = ½ (g_ad,bc + g_bc,ad - g_ac,bd - g_bd,ac) + Γ^e_bc Γ_ead - Γ^e_bd Γ_eac
    FrameRiemann CoordinateRiemann(const KerrNewmanParams& p, const double r, const double theta)
    {
        constexpr double h = 1e-3;
        // Derivatives only along r (index 1) and θ (index 2); the metric does not depend on t and φ.
        const auto at = [&](const double dr, const double dt) { return MetricTensor(p, r + dr, theta + dt); };
        const auto offset = [&](const std::size_t index, const double step)
        {
            return std::pair{index == 1 ? step : 0.0, index == 2 ? step : 0.0};
        };
        std::array<Metric, 4> first{};
        std::array<std::array<Metric, 4>, 4> second{};
        const Metric centre = at(0.0, 0.0);
        for (std::size_t c = 1; c <= 2; ++c)
        {
            const auto [pr, pt] = offset(c, h);
            const Metric plus = at(pr, pt);
            const Metric minus = at(-pr, -pt);
            for (std::size_t a = 0; a < 4; ++a)
            {
                for (std::size_t b = 0; b < 4; ++b)
                {
                    first[c][a][b] = (plus[a][b] - minus[a][b]) / (2.0 * h);
                    second[c][c][a][b] = (plus[a][b] - 2.0 * centre[a][b] + minus[a][b]) / (h * h);
                }
            }
        }
        {
            const Metric pp = at(h, h);
            const Metric pm = at(h, -h);
            const Metric mp = at(-h, h);
            const Metric mm = at(-h, -h);
            for (std::size_t a = 0; a < 4; ++a)
            {
                for (std::size_t b = 0; b < 4; ++b)
                {
                    second[1][2][a][b] = second[2][1][a][b] = (pp[a][b] - pm[a][b] - mp[a][b] + mm[a][b]) / (4.0 * h * h);
                }
            }
        }
        const Metric inverse = InverseMetric(centre);
        // Γ_abc = ½ (g_ab,c + g_ac,b - g_bc,a) and Γ^a_bc
        std::array<std::array<Vector4, 4>, 4> lower{};
        std::array<std::array<Vector4, 4>, 4> upper{};
        for (std::size_t a = 0; a < 4; ++a)
        {
            for (std::size_t b = 0; b < 4; ++b)
            {
                for (std::size_t c = 0; c < 4; ++c)
                {
                    lower[a][b][c] = 0.5 * (first[c][a][b] + first[b][a][c] - first[a][b][c]);
                }
            }
        }
        for (std::size_t a = 0; a < 4; ++a)
        {
            for (std::size_t b = 0; b < 4; ++b)
            {
                for (std::size_t c = 0; c < 4; ++c)
                {
                    for (std::size_t e = 0; e < 4; ++e) upper[a][b][c] += inverse[a][e] * lower[e][b][c];
                }
            }
        }
        FrameRiemann riemann{};
        for (std::size_t a = 0; a < 4; ++a)
        {
            for (std::size_t b = 0; b < 4; ++b)
            {
                for (std::size_t c = 0; c < 4; ++c)
                {
                    for (std::size_t d = 0; d < 4; ++d)
                    {
                        double value = 0.5 * (second[b][c][a][d] + second[a][d][b][c] - second[b][d][a][c] - second[a][c][b][d]);
                        for (std::size_t e = 0; e < 4; ++e)
                        {
                            value += upper[e][b][c] * lower[e][a][d] - upper[e][b][d] * lower[e][a][c];
                        }
                        riemann[a][b][c][d] = value;
                    }
                }
            }
        }
        return riemann;
    }

    Matrix3 NumericalTidalTensor(const KerrNewmanParams& p, const double r, const double theta, const Vector3& velocity)
    {
        const Tetrad tetrad = ObserverTetrad(p, r, theta, velocity);
        return ContractTidal(CoordinateRiemann(p, r, theta), tetrad);
    }

    void CheckTensor(const Matrix3& actual, const Matrix3& expected, const double margin)
    {
        for (std::size_t i = 0; i < 3; ++i)
        {
            for (std::size_t j = 0; j < 3; ++j)
            {
                INFO("E_" << i << j);
                CHECK(actual[i][j] == Approx(expected[i][j]).margin(margin));
            }
        }
    }

    Matrix3 Diagonal(const double x, const double y, const double z)
    {
        return {Vector3{x, 0.0, 0.0}, Vector3{0.0, y, 0.0}, Vector3{0.0, 0.0, z}};
    }

    double Trace(const Matrix3& m) { return m[0][0] + m[1][1] + m[2][2]; }
}

TEST_CASE("Tidal tensor: Schwarzschild static observer", "[physics][tidal]")
{
    const KerrNewmanParams p{.Mass = 1.0};
    for (const double r : {3.0, 6.0, 40.0})
    {
        INFO("r = " << r);
        const double scale = 1.0 / (r * r * r);
        CheckTensor(TidalTensor(p, r, 1.1, Vector3{}), Diagonal(-2.0 * scale, scale, scale), 1e-14);
    }
}

TEST_CASE("Tidal tensor: radial boosts in Schwarzschild leave it unchanged", "[physics][tidal]")
{
    // A radially moving observer sees the same tensor as the static one (the radial direction is a principal null
    // direction of the Weyl tensor); a raindrop at the horizon is no exception.
    const KerrNewmanParams p{.Mass = 1.0};
    const double r = 5.0;
    const Matrix3 rest = TidalTensor(p, r, kHalfPi, Vector3{});
    for (const double v : {-0.9, -0.3, 0.5})
    {
        INFO("v_r = " << v);
        CheckTensor(TidalTensor(p, r, kHalfPi, Vector3{v, 0.0, 0.0}), rest, 1e-14);
    }
    const auto raindrop = ObserverVelocity(p, ObserverKind::Raindrop, 2.5, kHalfPi);
    REQUIRE(raindrop);
    CheckTensor(TidalTensor(p, 2.5, kHalfPi, *raindrop), Diagonal(-2.0 / 15.625, 1.0 / 15.625, 1.0 / 15.625), 1e-13);
}

TEST_CASE("Tidal tensor: Reissner–Nordström static observer", "[physics][tidal]")
{
    // Radial -2M/r³ + 3Q²/r⁴, transverse M/r³ - Q²/r⁴
    const KerrNewmanParams p{.Mass = 1.0, .Charge = 0.6};
    const double r = 4.0;
    const double q2 = 0.36;
    const double radial = -2.0 / (r * r * r) + 3.0 * q2 / (r * r * r * r);
    const double transverse = 1.0 / (r * r * r) - q2 / (r * r * r * r);
    CheckTensor(TidalTensor(p, r, 0.8, Vector3{}), Diagonal(radial, transverse, transverse), 1e-14);
}

TEST_CASE("Tidal tensor: matches the Riemann tensor of the coordinate metric", "[physics][tidal]")
{
    struct Case
    {
        KerrNewmanParams Params;
        double R;
        double Theta;
        Vector3 Velocity;
    };
    const std::array cases{
        Case{{.Mass = 1.0}, 6.0, kHalfPi, {0.2, 0.0, 0.4}},
        Case{{.Mass = 1.0, .SpinParameter = 0.9}, 4.0, kHalfPi, {}},
        Case{{.Mass = 1.0, .SpinParameter = 0.9}, 3.0, 0.7, {0.3, -0.4, 0.5}},
        Case{{.Mass = 1.0, .SpinParameter = 0.6, .Charge = 0.5}, 5.0, 2.2, {-0.5, 0.1, -0.6}},
        Case{{.Mass = 1.0, .SpinParameter = 0.998}, 2.0, 1.3, {0.0, 0.0, 0.7}},
    };
    for (const auto& c : cases)
    {
        INFO("a = " << c.Params.SpinParameter << ", Q = " << c.Params.Charge << ", r = " << c.R << ", theta = " << c.Theta
             << ", v = (" << c.Velocity[0] << ", " << c.Velocity[1] << ", " << c.Velocity[2] << ")");
        const Matrix3 analytic = TidalTensor(c.Params, c.R, c.Theta, c.Velocity);
        const Matrix3 numerical = NumericalTidalTensor(c.Params, c.R, c.Theta, c.Velocity);
        const double scale = 1.0 / (c.R * c.R * c.R);
        CheckTensor(analytic, numerical, 1e-5 * scale);
    }
}

TEST_CASE("Tidal tensor: traceless in vacuum, for any observer", "[physics][tidal]")
{
    const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = 0.95};
    for (const Vector3 v : {Vector3{}, Vector3{0.6, 0.2, -0.3}, Vector3{0.0, 0.0, 0.99}})
    {
        const Matrix3 tidal = TidalTensor(p, 2.5, 1.0, v);
        CHECK(Trace(tidal) == Approx(0.0).margin(1e-12 * std::abs(tidal[0][0])));
        CHECK(tidal[0][1] == Approx(tidal[1][0]).margin(0.0));
    }
}

TEST_CASE("Tidal tensor: equatorial circular orbits", "[physics][tidal]")
{
    // Eigenvalues for a circular equatorial geodesic (Marck 1983) with K = (L - aE)²:
    //   -(M/r³)(2 + 3K/r²),  (M/r³)(1 + 3K/r²),  M/r³
    const auto check = [](const KerrNewmanParams& p, const double r, const double tolerance)
    {
        const auto orbit = MakeCircularOrbit<double>(p, r, OrbitDirection::Prograde);
        REQUIRE(orbit);
        const double k = std::pow(orbit->AngularMomentum - p.SpinParameter * orbit->Energy, 2.0);
        const double scale = p.Mass / (r * r * r);
        const auto axes = PrincipalAxes(TidalTensor(p, *orbit));
        CHECK(axes.Eigenvalues[0] == Approx(-scale * (2.0 + 3.0 * k / (r * r))).epsilon(tolerance));
        CHECK(axes.Eigenvalues[1] == Approx(scale).epsilon(tolerance));
        CHECK(axes.Eigenvalues[2] == Approx(scale * (1.0 + 3.0 * k / (r * r))).epsilon(tolerance));
        // The squeeze axis is perpendicular to the orbital plane (θ̂)
        CHECK(std::abs(axes.Axes[2][1]) == Approx(1.0).epsilon(1e-9));
    };
    SECTION("Schwarzschild") { check({.Mass = 1.0}, 8.0, 1e-12); }
    SECTION("Kerr") { check({.Mass = 1.0, .SpinParameter = 0.9}, 3.0, 1e-12); }
    SECTION("Miller's orbit around near-extremal Gargantua")
    {
        // a/M = 1 - 1.3e-14, r - M = 4e-5 M (docs/notes/interstellar_planets.md). The tensor stays of order 1/M²:
        // Miller's planet is deformed, not disrupted.
        check({.Mass = 1.0, .SpinParameter = 1.0 - 1.3e-14}, 1.0 + 4e-5, 1e-6);
    }
}

TEST_CASE("Tidal tensor: principal axes", "[physics][tidal]")
{
    const Matrix3 m{Vector3{2.0, 1.0, 0.0}, Vector3{1.0, 2.0, 0.0}, Vector3{0.0, 0.0, -3.0}};
    const auto axes = PrincipalAxes(m);
    CHECK(axes.Eigenvalues[0] == Approx(-3.0));
    CHECK(axes.Eigenvalues[1] == Approx(1.0));
    CHECK(axes.Eigenvalues[2] == Approx(3.0));
    for (std::size_t k = 0; k < 3; ++k)
    {
        // M x = λ x
        for (std::size_t i = 0; i < 3; ++i)
        {
            const double mx = m[i][0] * axes.Axes[k][0] + m[i][1] * axes.Axes[k][1] + m[i][2] * axes.Axes[k][2];
            CHECK(mx == Approx(axes.Eigenvalues[k] * axes.Axes[k][i]).margin(1e-12));
        }
        CHECK(Dot3(axes.Axes[k], axes.Axes[k]) == Approx(1.0));
    }
}

TEST_CASE("Tidal tensor: SI conversion", "[physics][tidal]")
{
    // A static observer 1000 km from a 10 solar-mass hole: radial 2GM/r³ in s⁻², stretching
    const double massKg = 10.0 * GPP::Units::kSolarMass;
    const double lengthM = GPP::Units::GravitationalRadius(massKg);
    const double rM = 1.0e6;
    const Matrix3 geometric = TidalTensor(KerrNewmanParams{.Mass = 1.0}, rM / lengthM, kHalfPi, Vector3{});
    const Matrix3 si = TidalTensorSi(geometric, lengthM, GPP::Units::kSpeedOfLight);
    const double newtonian = 2.0 * GPP::Units::kGravitationalConstant * massKg / (rM * rM * rM);
    CHECK(si[0][0] == Approx(-newtonian).epsilon(1e-12));
}
