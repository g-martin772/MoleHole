#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

import vulkan;
import glm;
import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

namespace
{
    const std::filesystem::path kRoot(MOLEHOLE_SOURCE_DIR);

    struct SplatRig
    {
        std::shared_ptr<Logger> Log = std::make_shared<Logger>();
        std::shared_ptr<VulkanDevice> Device = CreateHeadlessDevice(Log);
        std::shared_ptr<FileSystem> Files = std::make_shared<FileSystem>();
        std::unique_ptr<VulkanCommandPool> Pool = std::make_unique<VulkanCommandPool>(Device, Log, Device->GetQueueIndices().Graphics);

        static std::unique_ptr<SplatRig>& Slot()
        {
            static std::unique_ptr<SplatRig> rig;
            return rig;
        }

        static SplatRig* Get()
        {
            static bool failed = false;
            auto& slot = Slot();
            if (!slot && !failed)
            {
                try { slot = std::make_unique<SplatRig>(); }
                catch (const std::exception&) { failed = true; }
            }
            return slot.get();
        }

        void Submit(const std::function<void(vk::CommandBuffer)>& record) const
        {
            ImmediateSubmit(*Pool, Device->GetGraphicsQueue(), record);
        }

        template <typename T>
        std::vector<T> ReadBuffer(const vk::Buffer source, const std::size_t count) const
        {
            const auto bytes = count * sizeof(T);
            VulkanBuffer staging(Device, MakeReadbackBufferSpecification(bytes), Log);
            Submit([&](const vk::CommandBuffer cmd) { CopyBuffer(cmd, source, staging.GetBuffer(), bytes); });
            std::vector<T> out(count);
            staging.Read(out.data(), bytes);
            return out;
        }

        std::vector<float> ReadImage(VulkanImage& image, const std::uint32_t n, const std::uint32_t channels) const
        {
            const auto bytes = static_cast<std::size_t>(n) * n * n * channels * sizeof(float);
            VulkanBuffer staging(Device, MakeReadbackBufferSpecification(bytes), Log);
            Submit([&](const vk::CommandBuffer cmd)
            {
                CopyImageToBuffer(cmd, image.GetImage(), staging.GetBuffer(), {n, n, n}, vk::ImageLayout::eGeneral);
            });
            std::vector<float> out(bytes / sizeof(float));
            staging.Read(out.data(), bytes);
            return out;
        }
    };

    struct SplatRigTeardown : Catch::EventListenerBase
    {
        using Catch::EventListenerBase::EventListenerBase;
        void testRunEnded(const Catch::TestRunStats&) override
        {
            if (auto& slot = SplatRig::Slot(); slot && slot->Device) slot->Device->WaitIdle();
            SplatRig::Slot().reset();
        }
    };
    CATCH_REGISTER_LISTENER(SplatRigTeardown)

    struct Cloud
    {
        ParticleArrays Particles;
        double Mass{0.0};
        glm::dvec3 Momentum{0.0};
    };

    // Particles uniformly in [-half, half]^3 with masses in [0.5, 1.5] 1e20 kg, velocities up to 0.5 c, temperatures 1e5-1e6 K
    Cloud MakeCloud(const std::size_t count, const float half, const std::uint32_t seed)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> position(-half, half);
        std::uniform_real_distribution<float> mass(0.5e20f, 1.5e20f);
        std::uniform_real_distribution<float> velocity(-0.5f, 0.5f);
        std::uniform_real_distribution<float> temperature(1e5f, 1e6f);
        Cloud cloud;
        cloud.Particles.Resize(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const float m = mass(rng);
            cloud.Particles.PosMass[i] = glm::vec4(position(rng), position(rng), position(rng), m);
            cloud.Particles.VelTemp[i] = glm::vec4(velocity(rng), velocity(rng), velocity(rng), temperature(rng));
            cloud.Particles.Meta[i] = glm::uvec4(0u, 1u, 0u, static_cast<std::uint32_t>(i));
            cloud.Mass += m;
            cloud.Momentum += glm::dvec3(m) * glm::dvec3(glm::vec3(cloud.Particles.VelTemp[i]));
        }
        return cloud;
    }

    std::array<double, 5> LevelSums(const std::vector<float>& words, const ParticleGridLayout& layout, const int level)
    {
        std::array<double, 5> sum{};
        const std::size_t nodes = layout.NodeCount();
        for (std::size_t n = 0; n < nodes; ++n)
        {
            for (int c = 0; c < 5; ++c) sum[static_cast<std::size_t>(c)] += words[(static_cast<std::size_t>(level) * nodes + n) * 5 + c];
        }
        return sum;
    }

    struct GpuBuffers
    {
        std::array<std::unique_ptr<VulkanBuffer>, 4> Buffers;
        ParticleGpuBuffers View;
    };

    GpuBuffers Upload(SplatRig& rig, const ParticleArrays& particles)
    {
        GpuBuffers out;
        const auto count = static_cast<std::uint32_t>(particles.Size());
        const std::array<std::size_t, 4> sizes{count * sizeof(glm::vec4), count * sizeof(glm::vec4), count * sizeof(glm::uvec4), 16};
        for (std::size_t i = 0; i < 4; ++i)
        {
            out.Buffers[i] = std::make_unique<VulkanBuffer>(rig.Device, MakeStorageBufferSpecification(sizes[i], true), rig.Log);
        }
        out.Buffers[0]->Upload(particles.PosMass.data(), sizes[0]);
        out.Buffers[1]->Upload(particles.VelTemp.data(), sizes[1]);
        out.Buffers[2]->Upload(particles.Meta.data(), sizes[2]);
        const std::uint32_t state[4]{count, 0, 0, 0};
        out.Buffers[3]->Upload(state, sizeof(state));
        out.View = {out.Buffers[0]->GetBuffer(), out.Buffers[1]->GetBuffer(), out.Buffers[2]->GetBuffer(), out.Buffers[3]->GetBuffer(), count};
        return out;
    }

    std::unique_ptr<ParticleSplatter> MakeSplatter(SplatRig& rig)
    {
        ParticleSplatter::Options options;
        options.Device = rig.Device;
        options.FileSystem = rig.Files;
        options.Log = rig.Log;
        options.ShaderDirectory = kRoot / "shaders";
        options.CacheDirectory = std::filesystem::temp_directory_path() / "molehole_particle_splat_test_cache";
        return std::make_unique<ParticleSplatter>(std::move(options));
    }

    // Records the splat of `particles` into a fresh grid and returns the accumulator words as kg
    std::vector<float> RunSplat(SplatRig& rig, ParticleSplatter& splatter, const ParticleGpuBuffers& view, const ParticleGridBlockGpu& block,
                                const bool fixedPoint, const SplatFixedPoint& units, std::vector<float>* density = nullptr)
    {
        VulkanDescriptorAllocator descriptors(rig.Device, 16);
        rig.Submit([&](const vk::CommandBuffer cmd)
        {
            splatter.Record(cmd, [&](const vk::DescriptorSetLayout layout) { return descriptors.Allocate(layout); }, view, block, fixedPoint);
        });
        const auto layoutN = static_cast<std::size_t>(block.Info.x);
        const std::size_t words = static_cast<std::size_t>(kParticleGridLevels) * layoutN * layoutN * layoutN * kParticleGridChannels;
        auto raw = rig.ReadBuffer<std::uint32_t>(splatter.Accumulator(), words);
        std::vector<float> out(words);
        for (std::size_t i = 0; i < words; ++i)
        {
            if (!fixedPoint) out[i] = std::bit_cast<float>(raw[i]);
            else
            {
                const float scale = i % kParticleGridChannels == 4 ? static_cast<float>(kParticleTemperatureUnits) : 1.0f;
                out[i] = static_cast<float>(static_cast<std::int32_t>(raw[i])) * units.Unit * scale;
            }
        }
        if (density)
        {
            const auto textures = splatter.Textures();
            const auto n = static_cast<std::uint32_t>(layoutN);
            *density = rig.ReadImage(*textures[0], n, 4);
            auto fine = rig.ReadImage(*textures[2], n, 4);
            density->insert(density->end(), fine.begin(), fine.end());
        }
        return out;
    }

    constexpr int kResolution = 32;
}

TEST_CASE("The grid layout nests two levels around the disk and the block matches std140", "[rendering][splat]")
{
    const auto layout = MakeParticleGridLayout(100.0, 128);
    CHECK(layout.Resolution == 128);
    CHECK(layout.Half(0) == Approx(32.0f));
    CHECK(layout.Half(1) == Approx(160.0f));
    CHECK(layout.Levels[0].Voxel == Approx(0.5f));
    CHECK(layout.Levels[1].Voxel == Approx(2.5f));
    CHECK(layout.Levels[0].Min.x == Approx(-32.0f));
    CHECK(MakeParticleGridLayout(100.0, 4).Resolution == 8);
    CHECK(MakeParticleGridLayout(100.0, 4096).Resolution == 256);
    CHECK(sizeof(ParticleGridBlockGpu) == 64);

    const auto fixed = MakeSplatFixedPoint(2.0e20);
    CHECK(fixed.Unit * fixed.InverseUnit == Approx(1.0f));
    CHECK(fixed.Unit == Approx(2.0e20f / 4096.0f));
    CHECK(MakeSplatFixedPoint(0.0).Unit == 1.0f);

    const auto block = MakeParticleGridBlock(layout, 0.034f, 1.0e6f, 2, 14766.0, fixed, false);
    CHECK(block.Info.x == 128.0f);
    CHECK(block.Info.w == 2.0f);
    CHECK(block.Frame.y == 0.0f);
    CHECK(MakeParticleGridBlock(layout, 0.034f, 1.0e6f, 2, 14766.0, fixed, true).Frame.y == fixed.InverseUnit);
}

TEST_CASE("The CPU splat conserves mass, momentum and temperature weight", "[rendering][splat]")
{
    const auto layout = MakeParticleGridLayout(10.0, kResolution);
    const float margin = 1.6f * layout.Levels[1].Voxel * 2.0f;
    const auto cloud = MakeCloud(2000, layout.Half(1) - margin, 3);
    const float reference = 5.0e5f;
    double massT = 0.0;
    for (std::size_t i = 0; i < cloud.Particles.Size(); ++i) massT += cloud.Particles.PosMass[i].w * cloud.Particles.VelTemp[i].w / reference;

    for (const bool fixed : {false, true})
    {
        const auto units = MakeSplatFixedPoint(cloud.Mass / 2000.0);
        const auto words = SplatReference(cloud.Particles, layout, reference, fixed ? &units : nullptr);
        const auto coarse = LevelSums(words, layout, 1);
        INFO("fixed point " << fixed);
        CHECK(coarse[0] == Approx(cloud.Mass).epsilon(fixed ? 1e-4 : 1e-6));
        CHECK(coarse[1] == Approx(cloud.Momentum.x).epsilon(1e-3));
        CHECK(coarse[4] == Approx(massT).epsilon(1e-3));
    }
}

TEST_CASE("The kernel weights are a partition of unity and smooth in the position", "[rendering][splat]")
{
    const auto layout = MakeParticleGridLayout(10.0, kResolution);
    ParticleArrays one;
    one.Resize(1);
    one.Meta[0] = glm::uvec4(0u, 1u, 0u, 0u);
    one.VelTemp[0] = glm::vec4(0.0f);
    double previousPeak = 0.0;
    for (int k = 0; k <= 10; ++k)
    {
        const float x = 0.3f + 0.1f * static_cast<float>(k) * layout.Levels[1].Voxel;
        one.PosMass[0] = glm::vec4(x, 0.7f, -0.4f, 1.0f);
        const auto words = SplatReference(one, layout, 1.0f, nullptr);
        const auto sums = LevelSums(words, layout, 1);
        CHECK(sums[0] == Approx(1.0).epsilon(1e-6));
        double peak = 0.0;
        for (std::size_t n = 0; n < layout.NodeCount(); ++n) peak = std::max<double>(peak, words[(layout.NodeCount() + n) * 5]);
        CHECK(peak <= 0.75 * 0.75 * 0.75 + 1e-6);
        CHECK(peak >= 0.5 * 0.5 * 0.5 - 1e-6);
        (void)previousPeak;
        previousPeak = peak;
    }
}

TEST_CASE("The GPU splat conserves the particle mass (V-P8) and matches the CPU reference", "[rendering][splat][gpu]")
{
    auto* rig = SplatRig::Get();
    if (!rig) SKIP("no Vulkan device");
    auto splatter = MakeSplatter(*rig);
    splatter->Configure(kResolution);

    const auto layout = MakeParticleGridLayout(10.0, kResolution);
    const float reference = 5.0e5f;
    const float inside = layout.Half(1) - 3.0f * layout.Levels[1].Voxel;
    const auto cloud = MakeCloud(5000, inside, 11);
    const auto buffers = Upload(*rig, cloud.Particles);
    const auto units = MakeSplatFixedPoint(cloud.Mass / 5000.0);
    const double lengthM = 14766.0;

    SECTION("fixed point")
    {
        const auto block = MakeParticleGridBlock(layout, 0.034f, reference, 0, lengthM, units, true);
        std::vector<float> density;
        const auto gpu = RunSplat(*rig, *splatter, buffers.View, block, true, units, &density);
        const auto cpu = SplatReference(cloud.Particles, layout, reference, &units);

        for (int level = 0; level < kParticleGridLevels; ++level)
        {
            const auto g = LevelSums(gpu, layout, level);
            const auto c = LevelSums(cpu, layout, level);
            INFO("level " << level);
            CHECK(g[0] == Approx(c[0]).epsilon(1e-5));
            CHECK(g[1] == Approx(c[1]).margin(std::abs(c[0]) * 1e-4));
            CHECK(g[4] == Approx(c[4]).epsilon(1e-4));
        }
        CHECK(LevelSums(gpu, layout, 1)[0] == Approx(cloud.Mass).epsilon(1e-4));

        double worst = 0.0;
        double scale = 0.0;
        for (std::size_t i = 0; i < gpu.size(); i += 5) scale = std::max<double>(scale, std::abs(cpu[i]));
        for (std::size_t i = 0; i < gpu.size(); ++i) worst = std::max<double>(worst, std::abs(gpu[i] - cpu[i]));
        INFO("largest per-voxel difference " << worst / scale << " of the peak");
        CHECK(worst < 1e-3 * scale);

        const std::size_t n = static_cast<std::size_t>(kResolution);
        double resolved = 0.0;
        for (std::size_t v = 0; v < n * n * n; ++v)
        {
            resolved += static_cast<double>(density[v * 4]) * std::pow(static_cast<double>(layout.Levels[0].Voxel), 3) * lengthM * lengthM;
        }
        double fineMass = 0.0;
        for (std::size_t i = 0; i < cloud.Particles.Size(); ++i)
        {
            const glm::vec3 p(cloud.Particles.PosMass[i]);
            if (glm::all(glm::lessThan(glm::abs(p), glm::vec3(layout.Half(0) - 3.0f * layout.Levels[0].Voxel)))) fineMass += cloud.Particles.PosMass[i].w;
        }
        INFO("resolved fine level mass " << resolved << ", particles well inside " << fineMass);
        CHECK(resolved >= fineMass * (1.0 - 1e-3));
        CHECK(resolved <= LevelSums(cpu, layout, 0)[0] * (1.0 + 1e-3));
    }

    SECTION("float atomics")
    {
        if (!splatter->UsesFloatAtomics(false)) SKIP("no float atomics");
        const auto block = MakeParticleGridBlock(layout, 0.034f, reference, 0, lengthM, units, false);
        const auto gpu = RunSplat(*rig, *splatter, buffers.View, block, false, units);
        const auto cpu = SplatReference(cloud.Particles, layout, reference, nullptr);
        for (int level = 0; level < kParticleGridLevels; ++level)
        {
            CHECK(LevelSums(gpu, layout, level)[0] == Approx(LevelSums(cpu, layout, level)[0]).epsilon(1e-5));
        }
        double worst = 0.0;
        double scale = 0.0;
        for (std::size_t i = 0; i < gpu.size(); i += 5) scale = std::max<double>(scale, std::abs(cpu[i]));
        for (std::size_t i = 0; i < gpu.size(); ++i) worst = std::max<double>(worst, std::abs(gpu[i] - cpu[i]));
        CHECK(worst < 1e-4 * scale);
    }

    SECTION("fixed point is order independent")
    {
        const auto block = MakeParticleGridBlock(layout, 0.034f, reference, 0, lengthM, units, true);
        const auto first = RunSplat(*rig, *splatter, buffers.View, block, true, units);
        for (int run = 0; run < 3; ++run)
        {
            const auto again = RunSplat(*rig, *splatter, buffers.View, block, true, units);
            CHECK(std::memcmp(first.data(), again.data(), first.size() * sizeof(float)) == 0);
        }
    }
}

TEST_CASE("The transfer through a uniform slab is the closed form and does not depend on the step", "[rendering][transfer][gpu]")
{
    auto* rig = SplatRig::Get();
    if (!rig) SKIP("no Vulkan device");

    SetGlobalShaderDefines(Units::ShaderDefines());
    ShaderCompiler compiler(rig->Files, rig->Log);
    ShaderCompileOptions options;
    options.enableCache = false;
    options.includeDirectories = {kRoot / "shaders"};
    const auto compiled = compiler.Compile(ShaderSource{.path = kRoot / "tests" / "shaders" / "transfer_slab.comp", .stage = ShaderStage::Compute}, options);
    VulkanPipeline pipeline(rig->Device, compiled);

    struct Case { float Steps, Tau, Source; };
    const std::vector<Case> cases{{1, 0.01f, 3.0f}, {1, 1.0f, 3.0f}, {7, 1.0f, 3.0f}, {100, 1.0f, 3.0f}, {4096, 1.0f, 3.0f},
                                  {1, 20.0f, 0.5f}, {500, 20.0f, 0.5f}, {3, 1e-6f, 2.0f}, {1000, 1e-6f, 2.0f}, {64, 400.0f, 1.0f}};
    std::vector<glm::vec4> input;
    for (const auto& c : cases) input.emplace_back(c.Steps, c.Tau, c.Source, 0.0f);
    const auto bytes = input.size() * sizeof(glm::vec4);
    VulkanBuffer in(rig->Device, MakeStorageBufferSpecification(bytes, true), rig->Log);
    auto outSpec = MakeStorageBufferSpecification(bytes, false);
    outSpec.usage |= vk::BufferUsageFlagBits::eTransferSrc;
    VulkanBuffer out(rig->Device, outSpec, rig->Log);
    in.Upload(input.data(), bytes);
    VulkanDescriptorAllocator descriptors(rig->Device, 4);
    const auto set = descriptors.Allocate(pipeline.GetDescriptorSetLayouts()[0]);
    DescriptorSetWriter(rig->Device->GetDevice()).WriteStorageBuffer(set, 0, in.GetBuffer()).WriteStorageBuffer(set, 1, out.GetBuffer()).Update();
    rig->Submit([&](const vk::CommandBuffer cmd)
    {
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.GetPipeline());
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline.GetLayout(), 0, set, {});
        cmd.dispatch(static_cast<std::uint32_t>(input.size()), 1, 1);
    });
    const auto result = rig->ReadBuffer<glm::vec4>(out.GetBuffer(), input.size());
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const double transmittance = std::exp(-static_cast<double>(cases[i].Tau));
        const double radiance = cases[i].Source * (1.0 - transmittance);
        INFO("steps " << cases[i].Steps << ", tau " << cases[i].Tau);
        CHECK(result[i].x == Approx(radiance).epsilon(2e-4).margin(1e-6));
        CHECK(result[i].w == Approx(transmittance).epsilon(2e-4).margin(1e-6));
    }
}

TEST_CASE("The splat and tracer kernels compile with the grid bindings", "[rendering][shaders][splat]")
{
    SetGlobalShaderDefines(Units::ShaderDefines());
    ShaderCompileOptions options;
    options.enableCache = false;
    options.includeDirectories = {kRoot / "shaders", kRoot / "dependencies/gpp/shaders/pbr"};
    ShaderCompiler compiler(std::make_shared<FileSystem>(), std::make_shared<Logger>());
    const auto has = [](const CompiledShader& shader, const std::uint32_t binding)
    {
        return std::ranges::any_of(shader.reflection.descriptorBindings, [&](const auto& b) { return b.set == 0 && b.binding == binding; });
    };

    for (const int pass : {0, 1})
    {
        for (const bool floatAtomics : {false, true})
        {
            std::unordered_map<std::string, std::string> defines{{"PS_PASS", std::to_string(pass)}};
            if (floatAtomics) defines.emplace("PS_ATOMIC_FLOAT", "1");
            const auto shader = compiler.Compile(ShaderSource{.path = kRoot / "shaders" / "particle_splat.comp", .stage = ShaderStage::Compute, .defines = defines}, options);
            INFO("pass " << pass << " float " << floatAtomics);
            REQUIRE_FALSE(shader.spirv.empty());
            CHECK(has(shader, 4));
            CHECK(has(shader, 5));
            CHECK(has(shader, pass == 0 ? 0 : 6));
        }
    }

    for (const char* file : {"black_hole_rendering.comp", "flat_rendering.comp"})
    {
        for (const bool f64 : {false, true})
        {
            if (f64 && std::string_view(file) == "flat_rendering.comp") continue;
            GpuCapabilities caps;
            std::unordered_map<std::string, std::string> defines = PbrShaderDefines(caps, 32);
            if (f64) defines.emplace("GPP_PRECISION_F64", "1");
            const auto shader = compiler.Compile(ShaderSource{.path = kRoot / "shaders" / file, .stage = ShaderStage::Compute, .defines = defines}, options);
            INFO(file << " f64 " << f64);
            REQUIRE_FALSE(shader.spirv.empty());
            for (const std::uint32_t binding : {13u, 14u, 15u, 16u, 17u}) CHECK(has(shader, binding));
        }
    }
}
