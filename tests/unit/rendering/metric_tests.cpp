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

TEST_CASE("Spin and charge are not clamped, a naked singularity has no horizon", "[rendering][metric]")
{
    const auto p = MakeKerrNewmanParams(3, 1.0, 1.0, 1.0);
    CHECK(p.SpinParameter == Approx(1.0));
    CHECK(p.Charge == Approx(1.0));
    CHECK_FALSE(HasHorizon(p));
    CHECK(OuterHorizonRadius(p) == 0.0);
    CHECK(CaptureRadius(p) == Approx(kNakedCaptureRadiusM));

    const auto extremal = MakeKerrNewmanParams(1, 1.0, 1.0, 0.0);
    CHECK(HasHorizon(extremal));
    CHECK(OuterHorizonRadius(extremal) == Approx(1.0));
    CHECK(CaptureRadius(extremal) == Approx(1.0));

    const auto overspun = MakeKerrNewmanParams(1, 3.0, 1.2, 0.0);
    CHECK(overspun.SpinParameter == Approx(3.6));
    CHECK_FALSE(HasHorizon(overspun));

    const auto devScene = MakeKerrNewmanParams(3, 1.0, 0.65, 0.8);
    CHECK_FALSE(HasHorizon(devScene));
    CHECK(HasHorizon(MakeKerrNewmanParams(1, 1.0, 0.65, 0.8)));
}

TEST_CASE("The metric sees |a| and |Q|, the spin sign reverses the axis", "[rendering][metric]")
{
    const auto negative = MakeKerrNewmanParams(3, 2.0, -0.5, -0.25);
    CHECK(negative.SpinParameter == Approx(1.0));
    CHECK(negative.Charge == Approx(0.5));
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

TEST_CASE("FillMetricData writes the metric parameters and radii", "[rendering][metric]")
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

TEST_CASE("FillMetricData keeps a naked singularity finite", "[rendering][metric]")
{
    RaytraceParamsGpu params{};
    params.NumBlackHoles = 1;
    params.MetricType = static_cast<std::int32_t>(MetricKind::KerrNewman);
    params.DiskAccretionRate = 0.1f;
    params.BlackHoles[0].Mass = 1.0f;
    params.BlackHoles[0].MassSolar = 10.0f;
    params.BlackHoles[0].Spin = 0.65f;
    params.BlackHoles[0].Charge = 0.8f;

    FillMetricData(params);

    const auto& bh = params.BlackHoles[0];
    CHECK(bh.Spin == Approx(0.65f));
    CHECK(bh.Charge == Approx(0.8f));
    CHECK(bh.HorizonRadius == Approx(static_cast<float>(kNakedCaptureRadiusM)));
    CHECK(std::isfinite(bh.IscoRadius));
    CHECK(bh.IscoRadius > bh.HorizonRadius);
    CHECK(std::isfinite(params.DiskInfo[0].x));
    CHECK(params.DiskInfo[0].x > 0.0f);
}

TEST_CASE("FillMetricData recomputes only when a hole parameter changes", "[rendering][metric]")
{
    RaytraceParamsGpu params{};
    params.NumBlackHoles = 2;
    params.MetricType = static_cast<std::int32_t>(MetricKind::Kerr);
    params.DiskAccretionRate = 0.1f;
    const auto reset = [&]
    {
        params.BlackHoles[0] = {.Mass = 1.0f, .Spin = 0.5f, .MassSolar = 10.0f};
        params.BlackHoles[1] = {.Mass = 0.5f, .Spin = 0.9f, .MassSolar = 5.0f};
    };

    MetricCache cache;
    reset();
    FillMetricData(params, cache);
    CHECK(cache.Misses == 2);
    const auto first = params.BlackHoles[1];
    const auto firstProfile = params.DiskProfile;

    reset();
    FillMetricData(params, cache);
    CHECK(cache.Misses == 2);
    CHECK(params.BlackHoles[1].IscoRadius == first.IscoRadius);
    CHECK(std::memcmp(params.DiskProfile.data(), firstProfile.data(), sizeof(firstProfile)) == 0);

    reset();
    params.BlackHoles[1].Spin = 0.3f;
    FillMetricData(params, cache);
    CHECK(cache.Misses == 3);
    CHECK(params.BlackHoles[1].IscoRadius > first.IscoRadius);

    reset();
    params.MetricType = static_cast<std::int32_t>(MetricKind::Schwarzschild);
    FillMetricData(params, cache);
    CHECK(cache.Misses == 5);
    CHECK(params.BlackHoles[0].Spin == 0.0f);

    reset();
    params.DiskAccretionRate = 0.2f;
    FillMetricData(params, cache);
    CHECK(cache.Misses == 7);
}

TEST_CASE("Radiative efficiency is 1 - E_isco", "[rendering][metric][disk]")
{
    // Schwarzschild: 1 - sqrt(8/9); Kerr a = 0.998: about 0.32 (Thorne 1974)
    CHECK(RadiativeEfficiency({.Mass = 1.0}) == Approx(1.0 - std::sqrt(8.0 / 9.0)).epsilon(1e-5));
    CHECK(RadiativeEfficiency({.Mass = 1.0, .SpinParameter = 0.998}) == Approx(0.32).epsilon(0.02));
    CHECK(RadiativeEfficiency({.Mass = 1.0, .SpinParameter = 0.9}) > RadiativeEfficiency({.Mass = 1.0, .SpinParameter = 0.5}));
}

TEST_CASE("Page-Thorne flux vanishes at the ISCO and approaches the Newtonian disk far out", "[rendering][metric][disk]")
{
    for (const double a : {0.0, 0.9})
    {
        INFO(a);
        const KerrNewmanParams p{.Mass = 1.0, .SpinParameter = a};
        const double rIsco = ProgradeIscoRadius(p);
        const std::array<double, 4> radii{rIsco, 1.5 * rIsco, 3.0 * rIsco, 1e6};
        const auto flux = PageThorneFlux(p, radii);
        CHECK(flux[0] == Approx(0.0).margin(1e-12));
        CHECK(flux[1] > 0.0);
        CHECK(flux[2] > 0.0);
        // Shakura-Sunyaev limit 3 / (8 pi r^3) (1 - sqrt(r_in / r)) with G = M = 1; far enough out that the
        // relativistic correction to the sqrt(r_in / r) term is below the tolerance
        const double r = radii[3];
        const double newtonian = 3.0 / (8.0 * std::numbers::pi * r * r * r) * (1.0 - std::sqrt(rIsco / r));
        CHECK(flux[3] == Approx(newtonian).epsilon(2e-3));
    }
}

TEST_CASE("The Schwarzschild Page-Thorne flux peaks near 9.5 M", "[rendering][metric][disk]")
{
    std::vector<double> radii;
    for (double r = 6.0; r <= 20.0; r += 0.05) radii.push_back(r);
    const auto flux = PageThorneFlux({.Mass = 1.0}, radii);
    const auto peak = std::ranges::max_element(flux) - flux.begin();
    CHECK(radii[static_cast<std::size_t>(peak)] == Approx(9.55).margin(0.15));
}

TEST_CASE("Disk temperature scales as M^-1/4 at a fixed Eddington ratio", "[rendering][metric][disk]")
{
    const KerrNewmanParams p{.Mass = 1.0};
    const auto stellar = BuildDiskProfile(p, 10.0, 0.1, 20.0);
    const auto supermassive = BuildDiskProfile(p, 1e7, 0.1, 20.0);
    CHECK(stellar.PeakTemperature > 3e6);
    CHECK(stellar.PeakTemperature < 3e7);
    CHECK(stellar.PeakTemperature / supermassive.PeakTemperature == Approx(std::pow(1e6, 0.25)).epsilon(1e-6));
    // T ~ (accretion rate)^(1/4)
    CHECK(BuildDiskProfile(p, 10.0, 1.0, 20.0).PeakTemperature / stellar.PeakTemperature == Approx(std::pow(10.0, 0.25)).epsilon(1e-6));
    // Normalised table: zero at the ISCO, peak 1
    CHECK(stellar.Temperature.front() == Approx(0.0f).margin(1e-6));
    CHECK(*std::ranges::max_element(stellar.Temperature) == Approx(1.0f));
    CHECK(stellar.InnerRadius == Approx(6.0).epsilon(1e-6));
    CHECK(stellar.OuterRadius == Approx(20.0));
}

TEST_CASE("FillMetricData writes the disk profile in render units", "[rendering][metric][disk]")
{
    RaytraceParamsGpu params{};
    params.NumBlackHoles = 1;
    params.MetricType = static_cast<std::int32_t>(MetricKind::Schwarzschild);
    params.DiskAccretionRate = 0.1f;
    params.BlackHoles[0].Mass = 2.0f; // render units
    params.BlackHoles[0].MassSolar = 10.0f;

    FillMetricData(params);

    const auto& info = params.DiskInfo[0];
    CHECK(info.x > 3e6f);
    CHECK(info.y == Approx(12.0f));
    CHECK(info.z == Approx(static_cast<float>(kDiskOuterRadiusM) * 2.0f));
    CHECK(params.DiskProfile[0].x == Approx(0.0f).margin(1e-6f));
}
