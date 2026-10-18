#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

namespace
{
    struct GpuRig
    {
        std::shared_ptr<Logger> Log = std::make_shared<Logger>();
        std::shared_ptr<VulkanDevice> Device = CreateHeadlessDevice(Log);
        std::shared_ptr<FileSystem> Files = std::make_shared<FileSystem>();

        static std::unique_ptr<GpuRig>& Slot()
        {
            static std::unique_ptr<GpuRig> rig;
            return rig;
        }

        static GpuRig* Get()
        {
            static bool failed = false;
            auto& slot = Slot();
            if (!slot && !failed)
            {
                try
                {
                    slot = std::make_unique<GpuRig>();
                }
                catch (const std::exception&)
                {
                    failed = true;
                }
            }
            return slot.get();
        }
    };

    struct GpuRigTeardown : Catch::EventListenerBase
    {
        using Catch::EventListenerBase::EventListenerBase;
        void testRunEnded(const Catch::TestRunStats&) override
        {
            if (auto& slot = GpuRig::Slot(); slot && slot->Device) slot->Device->WaitIdle();
            GpuRig::Slot().reset();
        }
    };
    CATCH_REGISTER_LISTENER(GpuRigTeardown)

    struct Pair
    {
        BlackHoleComponent Hole;
        AccretionDiskComponent Disk;
        ParticleUnits Units;
        DiskSetup Setup;
        std::unique_ptr<ParticleSystem> Cpu;
        std::unique_ptr<ParticleSystem> Gpu;
    };

    std::unique_ptr<GpuParticleBackend> MakeBackend(GpuRig& rig)
    {
        GpuParticleOptions options;
        options.Device = rig.Device;
        options.FileSystem = rig.Files;
        options.Log = rig.Log;
        const std::filesystem::path root(MOLEHOLE_SOURCE_DIR);
        options.ShaderDirectory = root / "dependencies" / "gpp" / "shaders" / "particles";
        options.IncludeDirectories = {root / "shaders"};
        options.Force = ParticleForceKind::Custom;
        options.PostMacroStep = true;
        options.CacheDirectory = std::filesystem::temp_directory_path() / "molehole_particle_disk_test_cache";
        return std::make_unique<GpuParticleBackend>(std::move(options));
    }

    std::unique_ptr<Pair> MakePair(GpuRig& rig, const std::uint32_t capacity, const int count, const double eccentricity,
                                   const double ringRadius)
    {
        auto pair = std::make_unique<Pair>();
        pair->Hole.Mass = 10.0 * Units::kSolarMass;
        auto& disk = pair->Disk;
        disk.Model = DiskModel::Particles;
        disk.Alpha = 0.3;
        disk.AspectRatio = 0.2;
        disk.OuterRadius = 60.0;
        disk.FeedOuterEdge = false;
        disk.RingRadius = ringRadius;
        disk.RingEccentricity = eccentricity;
        disk.RingMass = 1.0e20;
        disk.RingParticles = count;
        disk.Seed = 3;
        pair->Units = ParticleUnits::Geometrized(pair->Hole.Mass);
        pair->Setup = MakeDiskSetup(disk, pair->Hole, pair->Units);

        ParticleSystemConfig config;
        config.Settings.Units = pair->Units;
        config.Settings.Capacity = capacity;
        config.Settings.MaxBin = 10;
        config.Settings.Eta = 0.01f;
        config.Settings.ReferenceWeight = static_cast<float>(DiskReferenceWeight(disk, pair->Hole, pair->Setup, pair->Units));
        config.BaseStep = 4.0 * pair->Units.Time;
        config.MaxMacroSteps = 1u << 20;

        ParticleSceneState state;
        state.Frame.Units = pair->Units;
        state.Emitters.push_back(*MakeDiskRing(disk, pair->Hole, pair->Setup, pair->Units, glm::dvec3(0.0)));
        state.Sinks.push_back(MakeDiskSink(pair->Hole, glm::dvec3(0.0)));

        pair->Cpu = std::make_unique<ParticleSystem>(std::make_unique<CpuParticleBackend>(), config);
        pair->Cpu->SetHooks(MakeDiskHooks());
        pair->Gpu = std::make_unique<ParticleSystem>(MakeBackend(rig), config);
        for (auto* system : {pair->Cpu.get(), pair->Gpu.get()})
        {
            system->SetForce(pair->Setup.Force);
            system->Consume(state);
            system->Prime();
        }
        return pair;
    }

    ParticleArrays Snapshot(ParticleSystem& system)
    {
        ParticleArrays out;
        system.Backend().Download(out);
        return out;
    }

    std::vector<std::size_t> BySerial(const ParticleArrays& p)
    {
        std::vector<std::size_t> order(p.Size());
        std::iota(order.begin(), order.end(), 0);
        std::ranges::sort(order, [&](const std::size_t a, const std::size_t b) { return p.Meta[a].w < p.Meta[b].w; });
        return order;
    }

    struct Difference
    {
        double Position{0.0};
        double Velocity{0.0};
        double Temperature{0.0};
    };

    Difference Compare(const ParticleArrays& a, const ParticleArrays& b)
    {
        REQUIRE(a.Size() == b.Size());
        const auto oa = BySerial(a);
        const auto ob = BySerial(b);
        Difference d;
        for (std::size_t k = 0; k < oa.size(); ++k)
        {
            const std::size_t i = oa[k];
            const std::size_t j = ob[k];
            d.Position = std::max<double>(d.Position, glm::length(glm::vec3(a.PosMass[i]) - glm::vec3(b.PosMass[j])) / glm::length(glm::vec3(a.PosMass[i])));
            d.Velocity = std::max<double>(d.Velocity, glm::length(glm::vec3(a.VelTemp[i]) - glm::vec3(b.VelTemp[j])) / glm::length(glm::vec3(a.VelTemp[i])));
            d.Temperature = std::max<double>(d.Temperature, std::abs(a.VelTemp[i].w - b.VelTemp[j].w) / std::max(a.VelTemp[i].w, 1.0f));
        }
        return d;
    }
}

TEST_CASE("The GPU kernels run the same disk physics as the CPU hooks", "[accretion][gpu][parity]")
{
    auto* rig = GpuRig::Get();
    if (!rig) SKIP("no Vulkan device");
    auto pair = MakePair(*rig, 1u << 14, 4000, 0.3, 20.0);

    const auto start = Compare(Snapshot(*pair->Cpu), Snapshot(*pair->Gpu));
    INFO("initial position difference " << start.Position);
    CHECK(start.Position < 1e-5);

    pair->Cpu->Advance(4.0 * pair->Units.Time);
    pair->Gpu->Advance(4.0 * pair->Units.Time);
    const auto one = Compare(Snapshot(*pair->Cpu), Snapshot(*pair->Gpu));
    INFO("one macro step: position " << one.Position << ", velocity " << one.Velocity << ", T " << one.Temperature);
    CHECK(one.Position < 1e-5);
    CHECK(one.Velocity < 1e-4);
    CHECK(one.Temperature < 1e-4);

    pair->Cpu->Advance(200.0 * pair->Units.Time);
    pair->Gpu->Advance(200.0 * pair->Units.Time);
    const auto many = Compare(Snapshot(*pair->Cpu), Snapshot(*pair->Gpu));
    INFO("fifty macro steps: position " << many.Position << ", velocity " << many.Velocity << ", T " << many.Temperature);
    CHECK(many.Position < 2e-3);
    CHECK(many.Velocity < 2e-2);

    const auto cpu = *pair->Cpu->Board()->Sink(kDiskSinkId);
    const auto gpu = *pair->Gpu->Board()->Sink(kDiskSinkId);
    CHECK(gpu.Count == cpu.Count);
}

TEST_CASE("The GPU disk plunges matter into the sink and books its energy like the CPU", "[accretion][gpu][bookkeeping]")
{
    auto* rig = GpuRig::Get();
    if (!rig) SKIP("no Vulkan device");
    auto pair = MakePair(*rig, 1u << 12, 1000, 0.9, 20.0);
    pair->Cpu->Advance(3000.0 * pair->Units.Time);
    pair->Gpu->Advance(3000.0 * pair->Units.Time);
    const auto cpu = *pair->Cpu->Board()->Sink(kDiskSinkId);
    const auto gpu = *pair->Gpu->Board()->Sink(kDiskSinkId);
    INFO("absorbed " << cpu.Count << " on the CPU, " << gpu.Count << " on the GPU");
    CHECK(cpu.Count > 900);
    CHECK(gpu.Count == cpu.Count);
    CHECK(gpu.Mass() == Approx(cpu.Mass()).epsilon(1e-6));
    CHECK(gpu.Energy(pair->Units) == Approx(cpu.Energy(pair->Units)).epsilon(5e-3));
    CHECK(gpu.AngularMomentum(pair->Units).y == Approx(cpu.AngularMomentum(pair->Units).y).epsilon(5e-3));
}

TEST_CASE("Disk particle throughput", "[.][accretion][gpu][bench]")
{
    auto* rig = GpuRig::Get();
    if (!rig) SKIP("no Vulkan device");
    for (const int count : {100000, 1000000})
    {
        auto pair = MakePair(*rig, static_cast<std::uint32_t>(count + 1024), count, 0.2, 30.0);
        pair->Gpu->Advance(40.0 * pair->Units.Time);
        pair->Gpu->Backend().Stats();
        constexpr int kMacroSteps = 100;
        const auto start = std::chrono::steady_clock::now();
        pair->Gpu->Advance(kMacroSteps * 4.0 * pair->Units.Time);
        ParticleArrays sync;
        pair->Gpu->Backend().Download(sync);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::println("{} particles: {} macro steps in {:.3f} s = {:.3f} ms per macro step, {:.1f} M particle-macro-steps/s",
                     count, kMacroSteps, seconds, 1000.0 * seconds / kMacroSteps,
                     static_cast<double>(sync.Size()) * kMacroSteps / seconds * 1e-6);
    }
}
