#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

namespace
{
    constexpr double kTwoPi = 2.0 * std::numbers::pi;

    // Schwarzschild in units of M: Omega = r^-3/2 per G M / c^3.
    long double ReferenceTheta(const double r, const double gravitationalTime, const double time, const double rate)
    {
        const long double omega = 1.0L / (std::pow(static_cast<long double>(r), 1.5L) * gravitationalTime);
        return static_cast<long double>(rate) * omega * static_cast<long double>(time);
    }

    double Radius(const int bin, const double inner, const double outer)
    {
        return inner * std::pow(outer / inner, static_cast<double>(bin) / (kDiskPhaseBins - 1));
    }

    const KerrNewmanParams kSchwarzschild{.Mass = 1.0, .SpinParameter = 0.0, .Charge = 0.0};
    const double kSgrAStarTg = Units::GravitationalRadius(4.297e6 * Units::kSolarMass) / Units::kSpeedOfLight;
}

TEST_CASE("The phase table is zero at t = 0 and for a stopped disk", "[rendering][diskphase]")
{
    const auto atZero = BuildDiskPhaseTable(kSchwarzschild, kSgrAStarTg, 6.0, 20.0, 0.0, 1.0);
    const auto stopped = BuildDiskPhaseTable(kSchwarzschild, kSgrAStarTg, 6.0, 20.0, 1.0e9, 0.0);
    for (int k = 0; k < kDiskPhaseBins; ++k)
    {
        CHECK(atZero.Bins[static_cast<std::size_t>(k)] == glm::vec4(0.0f));
        CHECK(stopped.Bins[static_cast<std::size_t>(k)] == glm::vec4(0.0f));
    }
}

TEST_CASE("The phase table reproduces Omega(r) t at the bin nodes after 1e9 s", "[rendering][diskphase][simclock]")
{
    constexpr double time = 1.0e9;
    const auto table = BuildDiskPhaseTable(kSchwarzschild, kSgrAStarTg, 6.0, 20.0, time, 1.0);
    for (const int k : {0, 1, 50, 127, 200, 254})
    {
        const double r = Radius(k, 6.0, 20.0);
        const auto expected = ReferenceTheta(r, kSgrAStarTg, time, 1.0);
        const auto expectedMod = std::fmod(expected, static_cast<long double>(kTwoPi));
        const auto& bin = table.Bins[static_cast<std::size_t>(k)];
        INFO("bin " << k << " theta " << static_cast<double>(expected));
        CHECK(static_cast<double>(bin.y) == Approx(static_cast<double>(expectedMod)).margin(1e-5));
        CHECK(static_cast<double>(bin.z) == Approx(static_cast<double>(ReferenceTheta(Radius(k + 1, 6.0, 20.0), kSgrAStarTg, time, 1.0) - expected)).epsilon(1e-6));
        const double total = static_cast<double>(bin.x) * kTwoPi + static_cast<double>(bin.y);
        CHECK(total == Approx(static_cast<double>(expected - std::floor(expected / (kTwoPi * kDiskPhaseTurnsModulus)) * kTwoPi * kDiskPhaseTurnsModulus)).margin(1e-4));
    }
}

TEST_CASE("Adjacent bins agree on the phase so the shader sees no seams", "[rendering][diskphase]")
{
    for (const double time : {0.37, 1.0e3, 1.0e9, 4.0e12})
    {
        const auto table = BuildDiskPhaseTable(kSchwarzschild, kSgrAStarTg, 6.0, 20.0, time, 1.0);
        for (int k = 0; k + 1 < kDiskPhaseBins; ++k)
        {
            const auto& a = table.Bins[static_cast<std::size_t>(k)];
            const auto& b = table.Bins[static_cast<std::size_t>(k + 1)];
            const double end = DiskPhaseAt(table, k, 1.0);
            const double start = DiskPhaseAt(table, k + 1, 0.0);
            const double tolerance = std::max(1e-6 * std::abs(static_cast<double>(a.z)), 1e-4);
            INFO("t " << time << " bin " << k);
            CHECK(std::abs(std::remainder(end - start, kTwoPi)) < tolerance + 1e-5 * std::abs(end));
            CHECK(std::abs(a.x - b.x) <= 8.0f + std::abs(a.z) / 6.28f);
        }
    }
}

TEST_CASE("The table follows the clock without drift: 600 ticks at 1e9 s equal the direct value", "[rendering][diskphase][simclock]")
{
    SimClock clock;
    clock.SetTime(1.0e9);
    for (int i = 0; i < 600; ++i) clock.Advance(1.0 / 60.0, 1.0);
    const auto stepped = BuildDiskPhaseTable(kSchwarzschild, kSgrAStarTg, 6.0, 20.0, clock.Time, 1.0);
    const auto direct = BuildDiskPhaseTable(kSchwarzschild, kSgrAStarTg, 6.0, 20.0, 1.0e9 + 10.0, 1.0);
    for (int k = 0; k < kDiskPhaseBins; k += 17)
    {
        const double a = DiskPhaseAt(stepped, k, 0.0);
        const double b = DiskPhaseAt(direct, k, 0.0);
        CHECK(std::abs(std::remainder(a - b, kTwoPi)) < 1e-6);
    }
}

TEST_CASE("The rate multiplies the clock and a negative time wraps correctly", "[rendering][diskphase]")
{
    const auto base = BuildDiskPhaseTable(kSchwarzschild, kSgrAStarTg, 6.0, 20.0, 5.0e3, 1.0);
    const auto fast = BuildDiskPhaseTable(kSchwarzschild, kSgrAStarTg, 6.0, 20.0, 5.0e3, 4.0);
    const auto later = BuildDiskPhaseTable(kSchwarzschild, kSgrAStarTg, 6.0, 20.0, 2.0e4, 1.0);
    for (int k = 0; k < kDiskPhaseBins; k += 31)
    {
        CHECK(std::abs(std::remainder(DiskPhaseAt(fast, k, 0.0) - DiskPhaseAt(later, k, 0.0), kTwoPi)) < 1e-4);
        CHECK(DiskPhaseAt(base, k, 0.0) >= 0.0);
    }
}

TEST_CASE("Kerr gas follows Omega = 1 / (r^3/2 + a)", "[rendering][diskphase]")
{
    constexpr double a = 0.9;
    constexpr double time = 250.0;
    const KerrNewmanParams kerr{.Mass = 1.0, .SpinParameter = a, .Charge = 0.0};
    const auto table = BuildDiskPhaseTable(kerr, kSgrAStarTg, 2.5, 20.0, time, 1.0);
    for (const int k : {0, 100, 254})
    {
        const double r = Radius(k, 2.5, 20.0);
        const double expected = time / ((std::pow(r, 1.5) + a) * kSgrAStarTg);
        CHECK(DiskPhaseAt(table, k, 0.0) == Approx(expected).epsilon(1e-5));
    }
}

TEST_CASE("FillDiskPhases uploads one table per hole and the inner-edge phase for the flat disk", "[rendering][diskphase]")
{
    Scene scene("Phases");
    const auto entity = scene.CreateEntity("BH", "BlackHole");
    scene.Registry().emplace<TransformComponent>(entity, TransformComponent{});
    scene.Registry().emplace<BlackHoleComponent>(entity, BlackHoleComponent{.Mass = Units::kSolarMass});

    RaytraceParamsGpu params{};
    RenderFrame frame;
    FillSceneData(params, scene, frame);
    params.MetricType = 0;
    params.DiskAccretionRate = 0.1f;
    FillMetricData(params);

    FillDiskPhases(params, 0.0, 1.0);
    CHECK(params.FlatPhase == 0.0f);
    CHECK(params.DiskPhase[0] == glm::vec4(0.0f));

    FillDiskPhases(params, 1.0e-3, 1.0);
    const double omegaIsco = 1.0 / (std::pow(6.0, 1.5) * Units::GravitationalRadius(Units::kSolarMass) / Units::kSpeedOfLight);
    CHECK(static_cast<double>(params.FlatPhase) == Approx(std::fmod(omegaIsco * 1.0e-3, kTwoPi)).margin(2e-3));
    CHECK(params.DiskPhase[0].z != 0.0f);
    CHECK(params.DiskPhase[kDiskPhaseBins] == glm::vec4(0.0f)); // no second hole
}
