#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import MoleHole;
import std;

using namespace MoleHole;
using Catch::Approx;

namespace
{
    // Bardeen, Press & Teukolsky (1972), eq. 2.21: prograde ISCO of Kerr for dimensionless spin chi.
    double BardeenPressTeukolskyIsco(const double chi)
    {
        const double z1 = 1.0 + std::cbrt(1.0 - chi * chi) * (std::cbrt(1.0 + chi) + std::cbrt(1.0 - chi));
        const double z2 = std::sqrt(3.0 * chi * chi + z1 * z1);
        return 3.0 + z2 - std::sqrt((3.0 - z1) * (3.0 + z1 + 2.0 * z2));
    }
}

TEST_CASE("Metric type zeroes the parameters it does not model", "[rendering][metric]")
{
    const auto schwarzschild = MakeKerrNewmanParams(0, 2.0, 0.5, 0.5);
    CHECK(schwarzschild.SpinParameter == 0.0);
    CHECK(schwarzschild.Charge == 0.0);

    const auto kerr = MakeKerrNewmanParams(1, 2.0, 0.5, 0.5);
    CHECK(kerr.SpinParameter == Approx(1.0));
    CHECK(kerr.Charge == 0.0);

    const auto reissnerNordstrom = MakeKerrNewmanParams(2, 2.0, 0.5, 0.5);
    CHECK(reissnerNordstrom.SpinParameter == 0.0);
    CHECK(reissnerNordstrom.Charge == Approx(1.0));

    const auto kerrNewman = MakeKerrNewmanParams(3, 2.0, 0.5, 0.5);
    CHECK(kerrNewman.SpinParameter == Approx(1.0));
    CHECK(kerrNewman.Charge == Approx(1.0));
}

TEST_CASE("Spin and charge are clamped so the horizon always exists", "[rendering][metric]")
{
    const auto p = MakeKerrNewmanParams(3, 1.0, 1.0, 1.0);
    const double magnitude = std::hypot(p.SpinParameter, p.Charge);
    CHECK(magnitude == Approx(kMaxHorizonParameter));
    CHECK(p.SpinParameter == Approx(p.Charge));
    CHECK(OuterHorizonRadius(p) > 1.0);
}

TEST_CASE("Outer horizon and ergosphere radii", "[rendering][metric]")
{
    CHECK(OuterHorizonRadius({.Mass = 1.0}) == Approx(2.0));
    CHECK(OuterHorizonRadius({.Mass = 1.0, .SpinParameter = 0.6}) == Approx(1.8));
    CHECK(OuterHorizonRadius({.Mass = 1.0, .Charge = 0.8}) == Approx(1.6));
    CHECK(OuterHorizonRadius({.Mass = 1.0, .SpinParameter = 0.6, .Charge = 0.48}) == Approx(1.64));

    const KerrNewmanParams kerr{.Mass = 1.0, .SpinParameter = 0.9};
    CHECK(ErgosphereRadius(kerr, std::numbers::pi / 2.0) == Approx(2.0));
    CHECK(ErgosphereRadius(kerr, 0.0) == Approx(OuterHorizonRadius(kerr)));
}

TEST_CASE("Prograde orbital frequency matches the Kerr closed form", "[rendering][metric]")
{
    for (const double a : {0.0, 0.5, 0.9})
    {
        const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = a};
        for (const double r : {4.0, 6.0, 15.0})
        {
            CHECK(ProgradeOrbitalFrequency(p, r) == Approx(1.0 / (std::pow(r, 1.5) + a)));
        }
    }
}

TEST_CASE("ISCO matches Bardeen-Press-Teukolsky for Kerr", "[rendering][metric]")
{
    CHECK(ProgradeIscoRadius({.Mass = 1.0}) == Approx(6.0).epsilon(1e-6));
    CHECK(ProgradeIscoRadius({.Mass = 2.0}) == Approx(12.0).epsilon(1e-6));
    for (const double chi : {0.3, 0.7, 0.9, 0.998})
    {
        CHECK(ProgradeIscoRadius({.Mass = 1.0, .SpinParameter = chi}) ==
              Approx(BardeenPressTeukolskyIsco(chi)).epsilon(1e-5));
    }
}

TEST_CASE("ISCO of extremal Reissner-Nordstrom is 4M", "[rendering][metric]")
{
    CHECK(ProgradeIscoRadius({.Mass = 1.0, .Charge = 1.0}) == Approx(4.0).epsilon(1e-4));
}

TEST_CASE("FillMetricData writes clamped parameters and radii", "[rendering][metric]")
{
    RaytraceParamsGpu params{};
    params.NumBlackHoles = 1;
    params.MetricType = static_cast<std::int32_t>(MetricKind::Kerr);
    params.BlackHoles[0].Mass = 1.0f;
    params.BlackHoles[0].Spin = 0.9f;
    params.BlackHoles[0].Charge = 0.5f;

    FillMetricData(params);

    const auto& bh = params.BlackHoles[0];
    CHECK(bh.Spin == Approx(0.9f));
    CHECK(bh.Charge == 0.0f);
    CHECK(bh.HorizonRadius == Approx(1.0 + std::sqrt(1.0 - 0.81)));
    CHECK(bh.IscoRadius == Approx(BardeenPressTeukolskyIsco(0.9)).epsilon(1e-4));
}
