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
    constexpr double kHoleMass = 10.0 * Units::kSolarMass;

    BlackHoleComponent Hole(const double mass = kHoleMass)
    {
        BlackHoleComponent hole;
        hole.Mass = mass;
        return hole;
    }

    AccretionDiskComponent Disk()
    {
        AccretionDiskComponent disk;
        disk.Model = DiskModel::Particles;
        disk.Alpha = 0.8;
        disk.AspectRatio = 0.12;
        disk.OuterRadius = 60.0;
        disk.MaxParticles = 40000;
        disk.FeedOuterEdge = false;
        return disk;
    }

    // CPU particle system around a hole at the origin, run in units of its t_g
    struct Rig
    {
        BlackHoleComponent HoleC;
        AccretionDiskComponent DiskC;
        ParticleUnits Units;
        DiskSetup Setup;
        std::unique_ptr<ParticleSystem> System;

        Rig(const BlackHoleComponent& hole, const AccretionDiskComponent& disk, const double baseStepGeometric,
            const std::uint32_t capacity = 1u << 16, const std::uint32_t maxBin = 10, const std::uint32_t threads = 0,
            const float eta = 0.01f)
            : HoleC(hole), DiskC(disk), Units(ParticleUnits::Geometrized(hole.Mass)),
              Setup(MakeDiskSetup(disk, hole, Units))
        {
            ParticleSystemConfig config;
            config.Settings.Units = Units;
            config.Settings.Capacity = capacity;
            config.Settings.MaxBin = maxBin;
            config.Settings.Eta = eta;
            config.Settings.ReferenceWeight = static_cast<float>(DiskReferenceWeight(disk, hole, Setup, Units));
            config.BaseStep = baseStepGeometric * Units.Time;
            config.MaxMacroSteps = 1u << 24;
            System = std::make_unique<ParticleSystem>(std::make_unique<CpuParticleBackend>(threads), config);
            System->SetHooks(MakeDiskHooks());
            System->SetForce(Setup.Force);
        }

        void Load(std::vector<ParticleEmitterDef> emitters)
        {
            ParticleSceneState state;
            state.Frame.Units = Units;
            state.Emitters = std::move(emitters);
            state.Sinks.push_back(MakeDiskSink(HoleC, glm::dvec3(0.0)));
            System->Consume(state);
        }

        void LoadDisk()
        {
            std::vector<ParticleEmitterDef> emitters;
            if (auto ring = MakeDiskRing(DiskC, HoleC, Setup, Units, glm::dvec3(0.0))) emitters.push_back(*ring);
            if (auto feed = MakeDiskFeed(DiskC, HoleC, Setup, Units, glm::dvec3(0.0))) emitters.push_back(*feed);
            Load(std::move(emitters));
        }

        void Run(const double geometric) { System->Advance(geometric * Units.Time); }

        ParticleArrays Snapshot() const
        {
            ParticleArrays out;
            System->Backend().Download(out);
            return out;
        }

        [[nodiscard]] ParticleAccumulatorTotals Sink() const
        {
            return *System->Board()->Sink(kDiskSinkId);
        }
    };

    double PwEnergy(const glm::dvec3& x, const glm::dvec3& v, const double mu, const double rs)
    {
        const double r = glm::length(x);
        return 0.5 * glm::dot(v, v) - mu / std::max(r - rs, 0.001 * rs);
    }

    // Circular orbit energy and angular momentum of the Paczynski-Wiita potential at radius R
    double CircularEnergy(const double R, const double mu, const double rs)
    {
        const double v = PwCircularSpeed(R, mu, rs);
        return 0.5 * v * v - mu / (R - rs);
    }

    double CircularL(const double R, const double mu, const double rs) { return R * PwCircularSpeed(R, mu, rs); }

    // Guiding radius of the circular orbit with the same energy (outside the ISCO), bisection
    double GuidingRadius(const double energy, const double mu, const double rs)
    {
        double low = 3.0 * rs;
        double high = 5000.0;
        if (energy <= CircularEnergy(low, mu, rs)) return low;
        for (int i = 0; i < 80; ++i)
        {
            const double mid = 0.5 * (low + high);
            (CircularEnergy(mid, mu, rs) < energy ? low : high) = mid;
        }
        return 0.5 * (low + high);
    }

    struct Moments
    {
        double MeanGuiding{0.0};
        double MeanEccentricity{0.0};
        std::size_t Count{0};
    };

    Moments Measure(const ParticleArrays& p, const double mu, const double rs)
    {
        Moments m;
        for (std::size_t i = 0; i < p.Size(); ++i)
        {
            const glm::dvec3 x(p.PosMass[i]);
            const glm::dvec3 v(p.VelTemp[i]);
            const double energy = PwEnergy(x, v, mu, rs);
            const double L = glm::length(glm::cross(x, v));
            const double guide = GuidingRadius(energy, mu, rs);
            const double Lc = CircularL(guide, mu, rs);
            m.MeanGuiding += guide;
            m.MeanEccentricity += std::sqrt(std::max(1.0 - (L * L) / (Lc * Lc), 0.0));
            ++m.Count;
        }
        if (m.Count > 0)
        {
            m.MeanGuiding /= static_cast<double>(m.Count);
            m.MeanEccentricity /= static_cast<double>(m.Count);
        }
        return m;
    }

    std::uint64_t Hash(const ParticleArrays& p) { return p.Hash(); }
}

TEST_CASE("AccretionDisk component round-trips through scene YAML and defaults to the analytic disk", "[accretion][scene]")
{
    RegisterComponents();
    CHECK(AccretionDiskComponent{}.Model == DiskModel::Analytic);

    Scene scene("DiskRoundTrip");
    const auto entity = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<TransformComponent>(entity, TransformComponent{});
    scene.Registry().emplace<BlackHoleComponent>(entity, Hole());
    AccretionDiskComponent disk = Disk();
    disk.RateMode = DiskRateMode::Eddington;
    disk.Eddington = 0.25;
    disk.InnerRadius = 7.5;
    disk.RingRadius = 33.0;
    disk.RingEccentricity = 0.4;
    disk.RingMass = 1.5e24;
    disk.RingParticles = 1234;
    disk.FeedBackToHole = true;
    disk.Seed = 99;
    scene.Registry().emplace<AccretionDiskComponent>(entity, disk);

    Scene loaded;
    loaded.DeserializeFromYaml(scene.SerializeToYaml());
    const auto entityLoaded = loaded.FindByGuid(scene.GuidOf(entity));
    REQUIRE(loaded.IsValid(entityLoaded));
    const auto& back = loaded.Registry().get<AccretionDiskComponent>(entityLoaded);
    CHECK(back.Model == DiskModel::Particles);
    CHECK(back.RateMode == DiskRateMode::Eddington);
    CHECK(back.Eddington == Approx(0.25));
    CHECK(back.Alpha == Approx(0.8));
    CHECK(back.AspectRatio == Approx(0.12));
    CHECK(back.InnerRadius == Approx(7.5));
    CHECK(back.OuterRadius == Approx(60.0));
    CHECK(back.MaxParticles == 40000);
    CHECK(back.FeedBackToHole);
    CHECK_FALSE(back.FeedOuterEdge);
    CHECK(back.RingRadius == Approx(33.0));
    CHECK(back.RingEccentricity == Approx(0.4));
    CHECK(back.RingMass == Approx(1.5e24));
    CHECK(back.RingParticles == 1234);
    CHECK(back.Seed == 99);
    CHECK(back.MassRate == Approx(disk.MassRate));
}

TEST_CASE("A tagged mass rate in Msun/yr loads as kg/s", "[accretion][scene][units]")
{
    RegisterComponents();
    const std::string yaml = R"(Scene: T
SchemaVersion: 2
Entities:
  - Guid: 5
    Components:
      Metadata: {Guid: 5, Name: H, TypeTag: BlackHole}
      Transform: {Position: [0, 0, 0], Rotation: [0, 0, 0, 1], Scale: [1, 1, 1]}
      BlackHole: {Mass: {value: 10, unit: Msun}, Spin: 0, Charge: 0, SpinAxis: [0, 1, 0]}
      AccretionDisk: {Model: Particles, MassRate: {value: 1.0e-8, unit: Msun/yr}}
)";
    Scene scene;
    scene.DeserializeFromYaml(yaml);
    const auto entity = scene.FindByGuid(5);
    REQUIRE(scene.IsValid(entity));
    CHECK(scene.Registry().get<AccretionDiskComponent>(entity).MassRate
          == Approx(1.0e-8 * Units::kSolarMass / Units::kJulianYear).epsilon(1e-6));
    CHECK(scene.Registry().get<BlackHoleComponent>(entity).Mass == Approx(10.0 * Units::kSolarMass));
}

TEST_CASE("The accretion ring template loads with a particle disk on the hole", "[accretion][scene][templates]")
{
    RegisterComponents();
    std::ifstream file(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "templates" / "accretion-ring.yaml");
    REQUIRE(file.good());
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    Scene scene;
    scene.DeserializeFromYaml(text);
    int found = 0;
    for (auto [entity, hole, disk] : scene.Registry().view<const BlackHoleComponent, const AccretionDiskComponent>().each())
    {
        ++found;
        CHECK(hole.Mass == Approx(10.0 * Units::kSolarMass));
        CHECK(disk.Model == DiskModel::Particles);
        CHECK(disk.RingParticles == 20000);
        CHECK(disk.RingRadius == Approx(40.0));
    }
    CHECK(found == 1);
}

TEST_CASE("Disk setup is expressed in the hole's geometrized units", "[accretion]")
{
    const auto hole = Hole();
    auto disk = Disk();
    const auto units = ParticleUnits::Geometrized(hole.Mass);
    const auto setup = MakeDiskSetup(disk, hole, units);
    CHECK(setup.Force.Mu == Approx(1.0).epsilon(1e-6));
    CHECK(setup.Force.SchwarzschildRadius == Approx(2.0));
    CHECK(setup.InnerRadius == Approx(6.0));
    CHECK(setup.OuterRadius == Approx(60.0));
    CHECK(setup.Force.Kind == static_cast<std::uint32_t>(ParticleForceKind::Custom));
    disk.InnerRadius = 8.0;
    CHECK(MakeDiskSetup(disk, hole, units).InnerRadius == Approx(8.0));

    disk.RateMode = DiskRateMode::Eddington;
    disk.Eddington = 1.0;
    const double edd = DiskMassRate(disk, hole.Mass);
    CHECK(edd * Units::kSpeedOfLight * Units::kSpeedOfLight * kDiskRadiativeEfficiency
          == Approx(Units::EddingtonLuminosity(hole.Mass)).epsilon(1e-9));
}

TEST_CASE("Analytic disks and holes without a disk start no particle system", "[accretion][module]")
{
    RegisterComponents();
    Scene scene("Analytic");
    const auto entity = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<TransformComponent>(entity, TransformComponent{});
    scene.Registry().emplace<BlackHoleComponent>(entity, Hole());
    AccretionBoard board;
    ParticleDiskModule module(board, std::make_shared<ParticleHub>());
    CHECK_FALSE(module.Prepare(scene, {}, true));
    scene.Registry().emplace<AccretionDiskComponent>(entity);
    CHECK_FALSE(module.Prepare(scene, {}, true));
    CHECK(module.System() == nullptr);
}

TEST_CASE("V-P4: without viscosity every particle conserves energy and angular momentum", "[accretion][particles][physics]")
{
    auto hole = Hole();
    auto disk = Disk();
    disk.Alpha = 0.0;
    disk.RingRadius = 30.0;
    disk.RingEccentricity = 0.35;
    disk.RingMass = 1.0e20;
    disk.RingParticles = 200;
    Rig rig(hole, disk, 10.0, 1u << 12, 10, 1, 0.002f);
    rig.LoadDisk();
    rig.System->Prime();
    const auto start = rig.Snapshot();
    REQUIRE(start.Size() == 200);
    const double mu = rig.Setup.Force.Mu;
    const double rs = rig.Setup.Force.SchwarzschildRadius;

    const double period = 2.0 * std::numbers::pi * std::pow(30.0, 1.5);
    rig.Run(20.0 * period);
    const auto end = rig.Snapshot();
    REQUIRE(end.Size() == 200);

    std::map<std::uint32_t, std::size_t> byTag;
    for (std::size_t i = 0; i < end.Size(); ++i) byTag[end.Meta[i].w] = i;
    double worstEnergy = 0.0;
    double worstMomentum = 0.0;
    for (std::size_t i = 0; i < start.Size(); ++i)
    {
        const std::size_t j = byTag.at(start.Meta[i].w);
        const glm::dvec3 x0(start.PosMass[i]);
        const glm::dvec3 v0(start.VelTemp[i]);
        const glm::dvec3 x1(end.PosMass[j]);
        const glm::dvec3 v1(end.VelTemp[j]);
        const double e0 = PwEnergy(x0, v0, mu, rs);
        const double e1 = PwEnergy(x1, v1, mu, rs);
        const double l0 = glm::length(glm::cross(x0, v0));
        const double l1 = glm::length(glm::cross(x1, v1));
        worstEnergy = std::max(worstEnergy, std::abs(e1 - e0) / std::abs(e0));
        worstMomentum = std::max(worstMomentum, std::abs(l1 - l0) / l0);
    }
    INFO("worst relative energy drift " << worstEnergy << ", angular momentum drift " << worstMomentum);
    CHECK(worstEnergy < 2e-4);
    CHECK(worstMomentum < 2e-4);
}

TEST_CASE("V-P7: the full disk run is deterministic and independent of how time is split", "[accretion][particles][determinism]")
{
    auto hole = Hole();
    auto disk = Disk();
    disk.FeedOuterEdge = true;
    disk.RingRadius = 30.0;
    disk.RingEccentricity = 0.2;
    disk.RingMass = 1.0e20;
    disk.RingParticles = 300;
    disk.MaxParticles = 2000;
    const auto run = [&](const std::vector<double>& slices, const std::uint32_t threads)
    {
        Rig rig(hole, disk, 10.0, 1u << 13, 10, threads);
        rig.LoadDisk();
        rig.System->Prime();
        for (const double slice : slices) rig.Run(slice);
        return Hash(rig.Snapshot());
    };
    const auto whole = run({4000.0}, 1);
    CHECK(run({4000.0}, 1) == whole);
    CHECK(run({4000.0}, 4) == whole);
    CHECK(run({500.0, 1500.0, 40.0, 1960.0}, 3) == whole);
}

TEST_CASE("Vertical structure settles at H = h R", "[accretion][particles][physics]")
{
    auto hole = Hole();
    auto disk = Disk();
    disk.AspectRatio = 0.1;
    disk.Alpha = 0.3;
    disk.RingRadius = 30.0;
    disk.RingEccentricity = 0.0;
    disk.RingMass = 1.0e20;
    disk.RingParticles = 4000;
    Rig rig(hole, disk, 5.0, 1u << 14, 10);
    rig.LoadDisk();
    rig.System->Prime();
    rig.Run(6000.0);
    const auto p = rig.Snapshot();
    double sum = 0.0;
    double radius = 0.0;
    for (std::size_t i = 0; i < p.Size(); ++i)
    {
        sum += static_cast<double>(p.PosMass[i].y) * p.PosMass[i].y;
        radius += std::hypot(p.PosMass[i].x, p.PosMass[i].z);
    }
    const double height = std::sqrt(sum / static_cast<double>(p.Size()));
    radius /= static_cast<double>(p.Size());
    INFO("rms height " << height << " at mean radius " << radius);
    CHECK(height / radius == Approx(0.1).epsilon(0.1));
}

TEST_CASE("SC-2: an eccentric ring circularises, drifts to the ISCO and plunges", "[accretion][particles][physics][sc2]")
{
    auto hole = Hole();
    auto disk = Disk();
    disk.RingRadius = 40.0;
    disk.RingEccentricity = 0.3;
    disk.RingMass = 1.0e20;
    disk.RingParticles = 3000;
    Rig rig(hole, disk, 10.0, 1u << 13, 10);
    rig.LoadDisk();
    rig.System->Prime();
    const double mu = rig.Setup.Force.Mu;
    const double rs = rig.Setup.Force.SchwarzschildRadius;

    std::vector<Moments> history;
    history.push_back(Measure(rig.Snapshot(), mu, rs));
    for (int k = 0; k < 14; ++k)
    {
        rig.Run(250.0);
        history.push_back(Measure(rig.Snapshot(), mu, rs));
    }
    for (std::size_t k = 0; k < history.size(); ++k)
    {
        UNSCOPED_INFO("t = " << 250 * k << " M: alive " << history[k].Count << ", guiding radius " << history[k].MeanGuiding
                    << ", e " << history[k].MeanEccentricity);
    }
    CHECK(history.front().MeanEccentricity > 0.2);
    for (std::size_t k = 1; k < history.size(); ++k)
    {
        if (history[k].Count < 2500) break;
        if (k >= 3) CHECK(history[k].MeanEccentricity < history[k - 3].MeanEccentricity + 0.01);
        CHECK(history[k].MeanGuiding < history[k - 1].MeanGuiding);
    }

    rig.Run(8000.0);
    const auto remaining = rig.Snapshot();
    const auto sink = rig.Sink();
    INFO("alive at the end " << remaining.Size() << ", absorbed " << sink.Count);
    CHECK(sink.Count >= 2990);
    CHECK(remaining.Size() + sink.Count == 3000);
}

TEST_CASE("V-P5: accretion bookkeeping adds exactly the absorbed energy and angular momentum", "[accretion][particles][bookkeeping]")
{
    auto hole = Hole();
    hole.Spin = 0.3;
    hole.SpinAxis = {0.0f, 1.0f, 0.0f};
    auto disk = Disk();
    disk.Alpha = 0.0;
    disk.RingRadius = 20.0;
    disk.RingEccentricity = 0.9;
    disk.RingMass = 1.0e21;
    disk.RingParticles = 500;
    Rig rig(hole, disk, 2.0, 1u << 12, 10, 0, 0.003f);
    rig.LoadDisk();
    rig.System->Prime();
    rig.Run(6000.0);
    const auto sink = rig.Sink();
    REQUIRE(sink.Count == 500);
    CHECK(sink.Mass() == Approx(1.0e21).epsilon(1e-6));

    const auto delta = AccretionDeltaOf(sink, rig.Units, glm::dvec3(0.0, 1.0, 0.0));
    CHECK(delta.RestMass == Approx(sink.Mass()));
    CHECK(delta.Energy == Approx(sink.Energy(rig.Units)));
    CHECK(delta.AngularMomentum == Approx(sink.AngularMomentum(rig.Units).y));
    CHECK(delta.AngularMomentum > 0.0);

    const auto update = ApplyAccretion(hole.Mass, hole.Spin, delta);
    const double c = Units::kSpeedOfLight;
    CHECK(update.Mass == Approx(hole.Mass + delta.RestMass + delta.Energy / (c * c)).epsilon(1e-15));
    const double j0 = hole.Spin * Units::kGravitationalConstant * hole.Mass * hole.Mass / c;
    const double j1 = update.Spin * Units::kGravitationalConstant * update.Mass * update.Mass / c;
    CHECK(std::abs((j1 - j0) - delta.AngularMomentum) < 1e-5 * std::abs(delta.AngularMomentum));
    CHECK(update.Mass > hole.Mass);
    CHECK(delta.Energy < 0.0);
}

TEST_CASE("Particles plunging from inside the ISCO keep the energy and angular momentum they start with", "[accretion][particles][bookkeeping]")
{
    auto hole = Hole();
    auto disk = Disk();
    disk.Alpha = 0.0;
    disk.RingRadius = 5.5;
    disk.RingEccentricity = 0.002;
    disk.RingMass = 1.0e21;
    disk.RingParticles = 200;
    Rig rig(hole, disk, 2.0, 1u << 12, 10, 0, 0.003f);
    rig.LoadDisk();
    rig.System->Prime();
    const auto start = rig.Snapshot();
    const double mu = rig.Setup.Force.Mu;
    const double rs = rig.Setup.Force.SchwarzschildRadius;
    const glm::dvec3 x(start.PosMass[0]);
    const glm::dvec3 v(start.VelTemp[0]);
    const double e0 = PwEnergy(x, v, mu, rs);
    const double l0 = glm::length(glm::cross(x, v));
    rig.Run(3000.0);
    const auto sink = rig.Sink();
    REQUIRE(sink.Count == 200);
    const double energyPerMass = sink.Raw[7] / sink.Raw[0];
    const double lPerMass = std::hypot(sink.Raw[4], sink.Raw[5], sink.Raw[6]) / sink.Raw[0];
    INFO("e = " << energyPerMass << " expected " << e0 << ", L = " << lPerMass << " expected " << l0);
    CHECK(lPerMass == Approx(l0).epsilon(1e-3));
    CHECK(energyPerMass == Approx(e0).epsilon(0.02));
    CHECK(e0 == Approx(CircularEnergy(6.0, mu, rs)).epsilon(0.02));
}

TEST_CASE("Spin saturates at a* = 0.998 and never reverses the sign of the torque", "[accretion][bookkeeping]")
{
    const double c = Units::kSpeedOfLight;
    const double g = Units::kGravitationalConstant;
    const double mass = 1.0e31;
    const double j = 0.99 * g * mass * mass / c;
    const auto update = ApplyAccretion(mass, 0.99, {1.0e29, -5.0e45, 5.0 * j});
    CHECK(update.Spin == Approx(kMaxAccretionSpin));
    const auto retro = ApplyAccretion(mass, 0.5, {0.0, 0.0, -0.1 * j});
    CHECK(retro.Spin < 0.5);
    CHECK(ApplyAccretion(mass, 0.5, {}).Spin == Approx(0.5));
}

TEST_CASE("V-P3: a fed disk reaches a steady mass flux and Sigma nu follows the thin-disk solution", "[accretion][particles][physics][vp3]")
{
    auto hole = Hole();
    auto disk = Disk();
    disk.FeedOuterEdge = true;
    disk.OuterRadius = 60.0;
    disk.MassRate = 1.0e-8 * Units::kSolarMass / Units::kJulianYear;
    disk.MaxParticles = 60000;
    Rig rig(hole, disk, 10.0, 1u << 16, 10);
    rig.LoadDisk();
    rig.System->Prime();
    const double drift = DiskDriftTime(rig.Setup);
    INFO("drift time " << drift << " M");

    rig.Run(3.0 * drift);

    const double mu = rig.Setup.Force.Mu;
    const double rs = rig.Setup.Force.SchwarzschildRadius;
    const double rin = rig.Setup.InnerRadius;
    const double mdot = rig.Setup.MassRate * rig.Units.Time; // kg per t_g
    constexpr int kBins = 8;
    const double low = 1.4 * rin;
    const double high = 0.65 * rig.Setup.OuterRadius;
    std::array<double, kBins> mass{};
    std::array<double, kBins> flux{};
    constexpr int kSnapshots = 24;
    for (int s = 0; s < kSnapshots; ++s)
    {
        rig.Run(drift / 60.0);
        const auto p = rig.Snapshot();
        for (std::size_t i = 0; i < p.Size(); ++i)
        {
            const double x = p.PosMass[i].x;
            const double z = p.PosMass[i].z;
            const double R = std::hypot(x, z);
            if (R < low || R >= high) continue;
            const double t = std::log(R / low) / std::log(high / low);
            const int bin = std::min(static_cast<int>(t * kBins), kBins - 1);
            const double vr = (x * p.VelTemp[i].x + z * p.VelTemp[i].z) / R;
            mass[bin] += p.PosMass[i].w;
            flux[bin] += -p.PosMass[i].w * vr;
        }
    }

    double worstFlux = 0.0;
    double worstSigma = 0.0;
    for (int b = 0; b < kBins; ++b)
    {
        const double r0 = low * std::pow(high / low, static_cast<double>(b) / kBins);
        const double r1 = low * std::pow(high / low, static_cast<double>(b + 1) / kBins);
        const double radius = std::sqrt(r0 * r1);
        const double dr = r1 - r0;
        const double sigma = mass[b] / kSnapshots / (2.0 * std::numbers::pi * radius * dr);
        const double through = flux[b] / kSnapshots / dr;
        const double omega = std::sqrt(mu / radius) / (radius - rs);
        const double nu = rig.Setup.Alpha * rig.Setup.AspectRatio * rig.Setup.AspectRatio * radius * radius * omega;
        const double expected = mdot / (3.0 * std::numbers::pi) * (1.0 - std::sqrt(rin / radius));
        const double fluxError = through / mdot - 1.0;
        const double sigmaError = sigma * nu / expected - 1.0;
        UNSCOPED_INFO("R = " << radius << ": flux/Mdot " << through / mdot << ", Sigma nu / expected " << sigma * nu / expected);
        worstFlux = std::max(worstFlux, std::abs(fluxError));
        worstSigma = std::max(worstSigma, std::abs(sigmaError));
    }
    const auto sink = rig.Sink();
    INFO("worst flux error " << worstFlux << ", worst Sigma nu error " << worstSigma);
    CHECK(worstFlux < 0.15);
    CHECK(worstSigma < 0.15);
    (void)sink;
}

TEST_CASE("Viscous heating gives the Novikov-Thorne-like temperature profile", "[accretion][particles][physics]")
{
    auto hole = Hole();
    auto disk = Disk();
    disk.RingRadius = 30.0;
    disk.RingEccentricity = 0.0;
    disk.RingMass = 1.0e20;
    disk.RingParticles = 500;
    Rig rig(hole, disk, 10.0, 1u << 12, 10);
    rig.LoadDisk();
    rig.System->Prime();
    rig.Run(2000.0);
    const auto p = rig.Snapshot();
    const double coefficient = rig.Setup.Force.User[kDiskTemperature];
    REQUIRE(coefficient > 0.0);
    double ratio = 0.0;
    for (std::size_t i = 0; i < p.Size(); ++i)
    {
        const double R = std::hypot(p.PosMass[i].x, p.PosMass[i].z);
        const double expected = std::pow(coefficient * (1.0 - std::sqrt(6.0 / R)) / (R * R * R), 0.25);
        ratio += p.VelTemp[i].w / expected;
    }
    ratio /= static_cast<double>(p.Size());
    INFO("mean T / T_eff " << ratio);
    CHECK(ratio == Approx(1.0).epsilon(0.1));
}

TEST_CASE("Stop and play restore the initial ring and a paused disk stays put", "[accretion][module]")
{
    RegisterComponents();
    Scene scene("Module");
    const auto entity = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = {1.0e5, 2.0e4, -3.0e4}});
    scene.Registry().emplace<BlackHoleComponent>(entity, Hole());
    auto disk = Disk();
    disk.RingRadius = 40.0;
    disk.RingEccentricity = 0.3;
    disk.RingMass = 1.0e20;
    disk.RingParticles = 2000;
    disk.Seed = 5;
    scene.Registry().emplace<AccretionDiskComponent>(entity, disk);

    const double tg = Units::GeometricTime(kHoleMass);
    AccretionBoard board;
    auto hub = std::make_shared<ParticleHub>();
    ParticleDiskModule module(board, hub);
    ParticleArrays particles;
    const auto hash = [&]
    {
        module.System()->Backend().Download(particles);
        return Hash(particles);
    };

    REQUIRE(module.Prepare(scene, {}, true));
    module.AdvanceNow();
    const auto initial = hash();
    REQUIRE(particles.Size() == 2000);
    CHECK(board.Primary().Particles == 2000);
    CHECK(board.Primary().Mass == Approx(1.0e20).epsilon(1e-5));

    module.Prepare(scene, {}, true);
    module.AdvanceNow();
    CHECK(hash() == initial);

    scene.SetSimTime(3000.0 * tg);
    REQUIRE(module.Prepare(scene, {}, true));
    CHECK(module.PlannedSeconds() == Approx(3000.0 * tg));
    module.AdvanceNow();
    const auto played = hash();
    CHECK(played != initial);
    CHECK(module.System()->SimTime() == Approx(3000.0 * tg).margin(ParticleDiskModule::kExportBaseStepGeometric * tg));

    scene.SetSimTime(3000.0 * tg);
    module.Prepare(scene, {}, true);
    module.AdvanceNow();
    CHECK(hash() == played);

    scene.SetSimTime(0.0);
    REQUIRE(module.Prepare(scene, {}, true));
    module.AdvanceNow();
    CHECK(hash() == initial);

    scene.SetSimTime(3000.0 * tg);
    module.Prepare(scene, {}, true);
    module.AdvanceNow();
    CHECK(hash() == played);
}

TEST_CASE("A limited advance reports it and drops the backlog", "[accretion][module]")
{
    RegisterComponents();
    Scene scene("Limited");
    const auto entity = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<TransformComponent>(entity, TransformComponent{});
    scene.Registry().emplace<BlackHoleComponent>(entity, Hole());
    auto disk = Disk();
    disk.RingRadius = 40.0;
    disk.RingMass = 1.0e20;
    disk.RingParticles = 100;
    scene.Registry().emplace<AccretionDiskComponent>(entity, disk);
    const double tg = Units::GeometricTime(kHoleMass);
    AccretionBoard board;
    ParticleDiskModule module(board, std::make_shared<ParticleHub>());
    REQUIRE(module.Prepare(scene, {}, false));
    scene.SetSimTime(1.0e6 * tg);
    REQUIRE(module.Prepare(scene, {}, false));
    CHECK(module.PlannedSeconds() == Approx(ParticleDiskModule::kInteractiveMacroSteps * ParticleDiskModule::kBaseStepGeometric * tg));
    module.AdvanceNow();
    CHECK(board.Primary().Limited);
    REQUIRE(module.Prepare(scene, {}, false));
    CHECK(module.PlannedSeconds() == 0.0);
}

TEST_CASE("Feeding back to the hole sends one recorded mass and spin command, off by default", "[accretion][module][bookkeeping]")
{
    RegisterComponents();
    const auto make = [](const bool feedBack)
    {
        auto scene = std::make_unique<Scene>("Feedback");
        const auto entity = scene->CreateEntity("BlackHole");
        scene->Registry().emplace<TransformComponent>(entity, TransformComponent{});
        auto hole = Hole();
        hole.Spin = 0.5;
        scene->Registry().emplace<BlackHoleComponent>(entity, hole);
        auto disk = Disk();
        disk.Alpha = 0.0;
        disk.RingRadius = 20.0;
        disk.RingEccentricity = 0.9;
        disk.RingMass = 1.0e24;
        disk.RingParticles = 300;
        disk.FeedBackToHole = feedBack;
        scene->Registry().emplace<AccretionDiskComponent>(entity, disk);
        return scene;
    };
    const double tg = Units::GeometricTime(kHoleMass);
    for (const bool feedBack : {false, true})
    {
        auto scene = make(feedBack);
        AccretionBoard board;
        ParticleDiskModule module(board, std::make_shared<ParticleHub>());
        std::vector<std::vector<Command>> sent;
        std::vector<CommandOptions> options;
        const DiskCommandSink sink = [&](std::vector<Command> commands, CommandOptions o)
        {
            sent.push_back(std::move(commands));
            options.push_back(std::move(o));
        };
        REQUIRE(module.Prepare(*scene, sink, true));
        scene->SetSimTime(8000.0 * tg);
        REQUIRE(module.Prepare(*scene, sink, true));
        module.AdvanceNow();
        if (!feedBack)
        {
            CHECK(sent.empty());
            continue;
        }
        REQUIRE(sent.size() == 1);
        REQUIRE(sent[0].size() == 2);
        const auto* mass = std::get_if<SetFieldCommand>(&sent[0][0]);
        const auto* spin = std::get_if<SetFieldCommand>(&sent[0][1]);
        REQUIRE(mass != nullptr);
        REQUIRE(spin != nullptr);
        CHECK(mass->Field == "Mass");
        CHECK(spin->Field == "Spin");
        CHECK(options[0].Undoable);
        CHECK(options[0].OnlyWhilePlaying);
        CHECK(std::get<double>(mass->Value) > kHoleMass);
        CHECK(std::get<double>(spin->Value) <= kMaxAccretionSpin);
    }
}

TEST_CASE("Disk hooks match the shader constants of the force parameters", "[accretion][particles]")
{
    const auto hole = Hole();
    const auto disk = Disk();
    const auto units = ParticleUnits::Geometrized(hole.Mass);
    const auto setup = MakeDiskSetup(disk, hole, units);
    const auto hooks = MakeDiskHooks();
    const glm::vec3 x(10.0f, 0.5f, -3.0f);
    const glm::vec3 a = hooks.Acceleration(x, glm::vec3(0.0f), setup.Force);
    const glm::vec3 reference = ParticleKernel::PaczynskiWiita(x, setup.Force.Mu, setup.Force.SchwarzschildRadius);
    CHECK(a.x == Approx(reference.x));
    CHECK(a.y == Approx(reference.y));
    CHECK(a.z == Approx(reference.z));
    CHECK(hooks.Potential(x, setup.Force) < 0.0f);
}

namespace
{
    struct KernelParticle
    {
        glm::vec3 Position;
        glm::vec3 Velocity;
        float Weight{1.0f};
        float Temperature{1.0e4f};
        float Age{0.0f};

        explicit KernelParticle(const glm::vec3& position, const glm::vec3& velocity) : Position(position), Velocity(velocity) {}

        void Relax(const AccretionDiskComponent& disk, const BlackHoleComponent& hole, const double seconds)
        {
            const auto units = ParticleUnits::Geometrized(hole.Mass);
            const auto setup = MakeDiskSetup(disk, hole, units);
            ParticleView view{Position, Velocity, Weight, Temperature, Age, 0u, 0u, 1u};
            MakeDiskHooks().PostMacroStep(view, static_cast<float>(seconds / units.Time), setup.Force);
        }
    };

    // Angular momentum about the +y axis per unit mass
    float AngularMomentum(const KernelParticle& p)
    {
        return p.Position.z * p.Velocity.x - p.Position.x * p.Velocity.z;
    }
}

TEST_CASE("The tidal torque takes angular momentum from circular gas beyond the truncation radius only", "[accretion][particles][physics]")
{
    auto disk = Disk();
    disk.Alpha = 0.5;
    const auto hole = Hole();
    const float R = 30.0f;
    const float vK = std::sqrt(R) / (R - 2.0f);
    const glm::vec3 position(R, 0.0f, 0.0f);
    const double step = 1000.0 * ParticleUnits::Geometrized(hole.Mass).Time;

    KernelParticle without(position, glm::vec3(0.0f, 0.0f, -vK));
    without.Relax(disk, hole, step);
    const float reference = AngularMomentum(without);

    disk.TruncationRadius = 20.0;
    KernelParticle circular(position, glm::vec3(0.0f, 0.0f, -vK));
    circular.Relax(disk, hole, step);
    CHECK(AngularMomentum(circular) < 0.9f * reference);

    // Gas well inside the truncation radius is untouched
    disk.TruncationRadius = 45.0;
    KernelParticle inside(position, glm::vec3(0.0f, 0.0f, -vK));
    inside.Relax(disk, hole, step);
    CHECK(AngularMomentum(inside) == Approx(reference).epsilon(1e-6));

    // A stream falling in (half the circular speed, mostly radial) keeps its angular momentum while it is far from circular
    disk.TruncationRadius = 20.0;
    const glm::vec3 infall(-0.6f * vK, 0.0f, -0.4f * vK);
    KernelParticle streamA(position, infall);
    KernelParticle streamB(position, infall);
    auto none = disk;
    none.TruncationRadius = 0.0;
    streamA.Relax(disk, hole, step);
    streamB.Relax(none, hole, step);
    CHECK(AngularMomentum(streamA) == Approx(AngularMomentum(streamB)).epsilon(1e-6));
}

TEST_CASE("Shock damping circularises eccentric gas faster and leaves circular gas alone", "[accretion][particles][physics]")
{
    auto disk = Disk();
    disk.Alpha = 0.5;
    const auto hole = Hole();
    const float R = 25.0f;
    const float vK = std::sqrt(R) / (R - 2.0f);
    const double step = 400.0 * ParticleUnits::Geometrized(hole.Mass).Time;
    const glm::vec3 position(R, 0.0f, 0.0f);

    const auto radialSpeed = [&](const glm::vec3& velocity, const double shock)
    {
        auto d = disk;
        d.ShockDamping = shock;
        KernelParticle p(position, velocity);
        p.Relax(d, hole, step);
        return std::abs(p.Velocity.x);
    };
    const glm::vec3 eccentric(-0.5f * vK, 0.0f, -0.8f * vK);
    CHECK(radialSpeed(eccentric, 40.0) < 0.5f * radialSpeed(eccentric, 0.0));
    const glm::vec3 nearly(-0.02f * vK, 0.0f, -vK);
    CHECK(radialSpeed(nearly, 40.0) == Approx(radialSpeed(nearly, 0.0)).epsilon(1e-6));
}

TEST_CASE("The warm start releases the requested mass in near circular rings with a steady disk profile", "[accretion][scene]")
{
    auto disk = Disk();
    disk.SeedMass = 4.0e14;
    disk.SeedParticles = 120000;
    disk.SeedOuter = 30.0;
    const auto hole = Hole();
    const auto units = ParticleUnits::Geometrized(hole.Mass);
    const auto setup = MakeDiskSetup(disk, hole, units);
    const auto rings = MakeDiskSeed(disk, hole, setup, units, glm::dvec3(0.0));
    REQUIRE(rings.size() == kDiskSeedRings);

    std::uint64_t particles = 0;
    std::vector<double> radii;
    std::vector<double> counts;
    for (const auto& ring : rings)
    {
        const auto& p = ring.Params;
        particles += static_cast<std::uint64_t>(p.Burst);
        CHECK(p.Shape == ParticleEmitterShape::Ring);
        CHECK(p.VelocityMode == ParticleVelocityMode::Azimuthal);
        CHECK(p.Rate == 0.0);
        CHECK(p.ParticleMass * disk.SeedParticles == Approx(disk.SeedMass).epsilon(1e-12));
        const double R = p.Radius / units.Length;
        CHECK(p.Speed / units.Velocity() == Approx(PwCircularSpeed(R, setup.Force.Mu, setup.Force.SchwarzschildRadius)).epsilon(1e-9));
        radii.push_back(R);
        counts.push_back(static_cast<double>(p.Burst));
    }
    CHECK(particles == Approx(static_cast<double>(disk.SeedParticles)).epsilon(2e-3));
    CHECK(radii.front() > 1.15 * setup.InnerRadius);
    CHECK(radii.back() < 30.0);
    CHECK(std::ranges::is_sorted(radii));
    // Particles per ring follow Sigma ~ R^-1/2 (1 - sqrt(r_in / R)) times the area of the ring
    double total = 0.0;
    std::vector<double> expected;
    for (const auto& ring : rings)
    {
        const double R = ring.Params.Radius / units.Length;
        const double width = ring.Params.Thickness / units.Length;
        const double weight = std::max(1.0 - std::sqrt(setup.InnerRadius / R), 0.02) / std::sqrt(R) * 2.0 * std::numbers::pi * R * width;
        expected.push_back(weight);
        total += weight;
    }
    for (std::size_t k = 0; k < rings.size(); ++k)
    {
        CHECK(counts[k] == Approx(expected[k] / total * disk.SeedParticles).margin(1.0));
    }
    CHECK(MakeDiskSeed(Disk(), hole, setup, units, glm::dvec3(0.0)).empty());
}

TEST_CASE("The new disk parameters round-trip through YAML and the shader's user slots", "[accretion][scene]")
{
    auto disk = Disk();
    disk.TruncationRadius = 28.0;
    disk.ViewRadius = 70.0;
    disk.ShockDamping = 55.0;
    disk.SeedMass = 3.0e14;
    disk.SeedOuter = 26.0;
    disk.SeedParticles = 5000;
    const auto hole = Hole();
    const auto setup = MakeDiskSetup(disk, hole, ParticleUnits::Geometrized(hole.Mass));
    const double rg = Units::GravitationalRadius(hole.Mass) / ParticleUnits::Geometrized(hole.Mass).Length;
    CHECK(setup.Force.User[kDiskTruncation] == Approx(static_cast<float>(28.0 * rg)));
    CHECK(setup.Force.User[kDiskTidal] > 0.0f);
    CHECK(setup.Force.User[kDiskShock] == Approx(55.0f));
    const auto plain = MakeDiskSetup(Disk(), hole, ParticleUnits::Geometrized(hole.Mass));
    CHECK(plain.Force.User[kDiskTruncation] == 0.0f);
    CHECK(plain.Force.User[kDiskShock] == 0.0f);
}
