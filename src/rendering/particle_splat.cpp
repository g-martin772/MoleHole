module MoleHole;

import :Rendering.ParticleSplat;
import :Rendering.ParticleStage;
import :Rendering.Pipeline;
import :Rendering.Data;
import :Simulation.AccretionDisk;
import :Simulation.Components;
import std;
import vulkan;
import glm;
import GPP;

using namespace GPP;

namespace MoleHole
{
    ParticleGridLayout MakeParticleGridLayout(const double outerRadius, const int resolution)
    {
        ParticleGridLayout layout;
        layout.Resolution = std::clamp(resolution, 8, 256);
        const double outer = std::max(outerRadius, 4.0);
        const std::array<double, kParticleGridLevels> half{kParticleGridFineHalf * outer, kParticleGridCoarseHalf * outer};
        for (int level = 0; level < kParticleGridLevels; ++level)
        {
            auto& out = layout.Levels[static_cast<std::size_t>(level)];
            out.Voxel = static_cast<float>(2.0 * half[static_cast<std::size_t>(level)] / layout.Resolution);
            out.Min = glm::vec3(-0.5f * static_cast<float>(layout.Resolution) * out.Voxel);
        }
        return layout;
    }

    SplatFixedPoint MakeSplatFixedPoint(const double meanMass)
    {
        SplatFixedPoint fixed;
        if (!(meanMass > 0.0) || !std::isfinite(meanMass)) return fixed;
        fixed.Unit = static_cast<float>(meanMass * 0x1p-12);
        fixed.InverseUnit = static_cast<float>(0x1p12 / meanMass);
        return fixed;
    }

    ParticleGridBlockGpu MakeParticleGridBlock(const ParticleGridLayout& layout, const float kappa,
                                               const float referenceTemperature, const int holeIndex, const double lengthM,
                                               const SplatFixedPoint& fixed, const bool fixedPoint)
    {
        ParticleGridBlockGpu block;
        for (int level = 0; level < kParticleGridLevels; ++level)
        {
            const auto& source = layout.Levels[static_cast<std::size_t>(level)];
            block.Min[level] = glm::vec4(source.Min, source.Voxel);
        }
        block.Info = glm::vec4(static_cast<float>(layout.Resolution), kappa, referenceTemperature, static_cast<float>(holeIndex));
        block.Frame = glm::vec4(static_cast<float>(lengthM), fixedPoint ? fixed.InverseUnit : 0.0f, fixedPoint ? fixed.Unit : 0.0f, 0.0f);
        return block;
    }

    std::vector<float> SplatReference(const ParticleArrays& particles, const ParticleGridLayout& layout,
                                      const float referenceTemperature, const SplatFixedPoint* fixed)
    {
        const std::size_t nodes = layout.NodeCount();
        const std::size_t words = static_cast<std::size_t>(kParticleGridLevels) * nodes * kParticleGridChannels;
        std::vector<double> sum(fixed ? 0 : words, 0.0);
        std::vector<std::int32_t> counts(fixed ? words : 0, 0);
        const int res = layout.Resolution;

        const auto add = [&](const std::size_t index, const float value, const float scale)
        {
            if (fixed)
            {
                const auto rounded = static_cast<std::int32_t>(std::nearbyint(value * fixed->InverseUnit * scale));
                counts[index] = static_cast<std::int32_t>(static_cast<std::uint32_t>(counts[index]) + static_cast<std::uint32_t>(rounded));
            }
            else sum[index] += static_cast<double>(value);
        };

        const auto deposit = [&](const int level, const glm::vec3& position, const float mass, const glm::vec3& velocity,
                                 const float temperature)
        {
            const auto& spec = layout.Levels[static_cast<std::size_t>(level)];
            const glm::vec3 cell = (position - spec.Min) / spec.Voxel;
            const glm::ivec3 nearest(std::floor(cell.x), std::floor(cell.y), std::floor(cell.z));
            const glm::vec3 d = cell - glm::vec3(nearest) - 0.5f;
            const glm::vec3 w0 = 0.5f * (0.5f - d) * (0.5f - d);
            const glm::vec3 w1 = 0.75f - d * d;
            const glm::vec3 w2 = 0.5f * (0.5f + d) * (0.5f + d);
            const std::size_t base = static_cast<std::size_t>(level) * nodes;
            for (int k = 0; k < 3; ++k)
            {
                const int z = nearest.z + k - 1;
                if (z < 0 || z >= res) continue;
                const float wz = k == 0 ? w0.z : (k == 1 ? w1.z : w2.z);
                for (int j = 0; j < 3; ++j)
                {
                    const int y = nearest.y + j - 1;
                    if (y < 0 || y >= res) continue;
                    const float wy = j == 0 ? w0.y : (j == 1 ? w1.y : w2.y);
                    for (int i = 0; i < 3; ++i)
                    {
                        const int x = nearest.x + i - 1;
                        if (x < 0 || x >= res) continue;
                        const float wx = i == 0 ? w0.x : (i == 1 ? w1.x : w2.x);
                        const float m = mass * wx * wy * wz;
                        const std::size_t node = (base + (static_cast<std::size_t>(z) * res + y) * res + x) * kParticleGridChannels;
                        add(node, m, 1.0f);
                        add(node + 1, m * velocity.x, 1.0f);
                        add(node + 2, m * velocity.y, 1.0f);
                        add(node + 3, m * velocity.z, 1.0f);
                        add(node + 4, m * temperature, 1.0f / static_cast<float>(kParticleTemperatureUnits));
                    }
                }
            }
        };

        for (std::size_t i = 0; i < particles.Size(); ++i)
        {
            if (!ParticleArrays::Alive(particles.Meta[i])) continue;
            const glm::vec3 position(particles.PosMass[i]);
            const float mass = particles.PosMass[i].w;
            const glm::vec3 velocity(particles.VelTemp[i]);
            const float temperature = particles.VelTemp[i].w / std::max(referenceTemperature, 1e-6f);
            deposit(1, position, mass, velocity, temperature);
            if (glm::all(glm::lessThan(glm::abs(position), glm::vec3(layout.Half(0))))) deposit(0, position, mass, velocity, temperature);
        }

        std::vector<float> out(words);
        for (std::size_t i = 0; i < words; ++i)
        {
            if (fixed)
            {
                const float scale = i % kParticleGridChannels == 4 ? static_cast<float>(kParticleTemperatureUnits) : 1.0f;
                out[i] = static_cast<float>(counts[i]) * fixed->Unit * scale;
            }
            else out[i] = static_cast<float>(sum[i]);
        }
        return out;
    }

    struct ParticleSplatter::Impl
    {
        struct Variant
        {
            CompiledShader Compiled;
            std::unique_ptr<VulkanPipeline> Pipeline;
        };

        struct Resources
        {
            int Resolution{0};
            std::shared_ptr<VulkanBuffer> Accumulator;
            std::array<std::shared_ptr<VulkanImage>, 4> Textures;
        };

        Options Opt;
        ShaderCompiler Compiler;
        std::unique_ptr<VulkanCommandPool> Pool;
        std::shared_ptr<VulkanBuffer> BlockBuffer;
        Resources Current;
        std::deque<std::pair<std::uint64_t, Resources>> Retired;
        std::uint64_t Frame{0};
        bool AtomicFloat{false};
        std::array<Variant, 4> Variants; // pass * 2 + float
        std::optional<std::string> Error;

        explicit Impl(Options options)
            : Opt(std::move(options)), Compiler(Opt.FileSystem, Opt.Log)
        {
            AtomicFloat = Opt.PreferAtomicFloat && Opt.Device->GetCapabilities().Has(GpuCapability::kAtomicFloat);
            Pool = std::make_unique<VulkanCommandPool>(Opt.Device, Opt.Log, Opt.Device->GetQueueIndices().Graphics);
            BlockBuffer = std::make_shared<VulkanBuffer>(Opt.Device, MakeUniformBufferSpecification(sizeof(ParticleGridBlockGpu), false), Opt.Log);
        }

        std::shared_ptr<VulkanImage> MakeImage(const int resolution, const vk::Format format, std::string name) const
        {
            const auto n = static_cast<std::uint32_t>(resolution);
            return std::make_shared<VulkanImage>(
                Opt.Device,
                VulkanImageSpecification{
                    .extent = {n, n, n},
                    .format = format,
                    .type = vk::ImageType::e3D,
                    .viewType = vk::ImageViewType::e3D,
                    .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc,
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .createSampler = true,
                    .samplerFilter = vk::Filter::eLinear,
                    .samplerAddressMode = vk::SamplerAddressMode::eClampToEdge,
                    .debugName = std::move(name)},
                Opt.Log);
        }

        Variant& Get(const int pass, const bool useFloat)
        {
            auto& variant = Variants[static_cast<std::size_t>(pass * 2 + (useFloat ? 1 : 0))];
            if (variant.Pipeline) return variant;
            std::unordered_map<std::string, std::string> defines{{"PS_PASS", std::to_string(pass)}};
            if (useFloat) defines.emplace("PS_ATOMIC_FLOAT", "1");
            ShaderSource source{.path = Opt.ShaderDirectory / "particle_splat.comp", .stage = ShaderStage::Compute,
                                .entryPoint = "main", .defines = std::move(defines)};
            ShaderCompileOptions compile;
            compile.cacheDirectory = Opt.CacheDirectory;
            compile.includeDirectories.push_back(Opt.ShaderDirectory);
            variant.Compiled = Compiler.Compile(source, compile);
            variant.Pipeline = std::make_unique<VulkanPipeline>(Opt.Device, variant.Compiled);
            return variant;
        }

        static bool Has(const CompiledShader& shader, const std::uint32_t binding)
        {
            return std::ranges::any_of(shader.reflection.descriptorBindings,
                                       [binding](const auto& b) { return b.set == 0 && b.binding == binding; });
        }

        void Barrier(const vk::CommandBuffer cmd, const vk::PipelineStageFlags source, const vk::AccessFlags sourceAccess) const
        {
            const vk::MemoryBarrier barrier{sourceAccess, vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite
                                                              | vk::AccessFlagBits::eUniformRead};
            cmd.pipelineBarrier(source, vk::PipelineStageFlagBits::eComputeShader, {}, 1, &barrier, 0, nullptr, 0, nullptr);
        }

        void WriteBlockCommand(const vk::CommandBuffer cmd, const ParticleGridBlockGpu& block) const
        {
            cmd.updateBuffer(BlockBuffer->GetBuffer(), 0, sizeof(block), &block);
            Barrier(cmd, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);
        }
    };

    ParticleSplatter::ParticleSplatter(Options options)
    {
        if (!options.Device) throw std::runtime_error("ParticleSplatter needs a device");
        m_Impl = std::make_unique<Impl>(std::move(options));
        Configure(1);
        WriteBlock(ParticleGridBlockGpu{});
    }

    ParticleSplatter::~ParticleSplatter()
    {
        if (m_Impl && m_Impl->Opt.Device) m_Impl->Opt.Device->WaitIdle();
    }

    int ParticleSplatter::Resolution() const noexcept
    {
        return m_Impl->Current.Resolution;
    }

    bool ParticleSplatter::UsesFloatAtomics(const bool fixedPoint) const noexcept
    {
        return m_Impl->AtomicFloat && !fixedPoint;
    }

    vk::Buffer ParticleSplatter::Block() const
    {
        return m_Impl->BlockBuffer->GetBuffer();
    }

    vk::Buffer ParticleSplatter::Accumulator() const
    {
        return m_Impl->Current.Accumulator->GetBuffer();
    }

    std::array<std::shared_ptr<VulkanImage>, 4> ParticleSplatter::Textures() const
    {
        return m_Impl->Current.Textures;
    }

    bool ParticleSplatter::Ready() const noexcept
    {
        return !m_Impl->Error;
    }

    std::optional<std::string> ParticleSplatter::Failure() const
    {
        return m_Impl->Error;
    }

    void ParticleSplatter::Tick()
    {
        auto& impl = *m_Impl;
        ++impl.Frame;
        while (!impl.Retired.empty() && impl.Frame - impl.Retired.front().first > 6) impl.Retired.pop_front();
    }

    void ParticleSplatter::Configure(const int resolution)
    {
        auto& impl = *m_Impl;
        const int n = std::max(resolution, 1);
        if (impl.Current.Resolution == n) return;
        if (impl.Current.Resolution > 0) impl.Retired.emplace_back(impl.Frame, std::move(impl.Current));

        Impl::Resources next;
        next.Resolution = n;
        const auto nodes = static_cast<vk::DeviceSize>(n) * n * n;
        auto spec = MakeStorageBufferSpecification(nodes * kParticleGridChannels * kParticleGridLevels * sizeof(std::uint32_t), false);
        spec.usage |= vk::BufferUsageFlagBits::eTransferSrc;
        next.Accumulator = std::make_shared<VulkanBuffer>(impl.Opt.Device, spec, impl.Opt.Log);
        for (int level = 0; level < kParticleGridLevels; ++level)
        {
            next.Textures[static_cast<std::size_t>(level) * 2] = impl.MakeImage(n, vk::Format::eR32G32B32A32Sfloat, std::format("ParticleGrid{}A", level));
            next.Textures[static_cast<std::size_t>(level) * 2 + 1] = impl.MakeImage(n, vk::Format::eR32Sfloat, std::format("ParticleGrid{}B", level));
        }
        ImmediateSubmit(*impl.Pool, impl.Opt.Device->GetBackgroundQueue(), [&](const vk::CommandBuffer cmd)
        {
            for (std::size_t i = 0; i < next.Textures.size(); ++i)
            {
                const auto format = i % 2 == 0 ? vk::Format::eR32G32B32A32Sfloat : vk::Format::eR32Sfloat;
                TransitionImageLayout(cmd, next.Textures[i]->GetImage(), format, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
            }
        });
        impl.Current = std::move(next);
    }

    void ParticleSplatter::WriteBlock(const ParticleGridBlockGpu& block)
    {
        auto& impl = *m_Impl;
        ImmediateSubmit(*impl.Pool, impl.Opt.Device->GetBackgroundQueue(), [&](const vk::CommandBuffer cmd)
        {
            cmd.updateBuffer(impl.BlockBuffer->GetBuffer(), 0, sizeof(block), &block);
        });
    }

    void ParticleSplatter::RecordBlock(const vk::CommandBuffer commandBuffer, const ParticleGridBlockGpu& block)
    {
        m_Impl->WriteBlockCommand(commandBuffer, block);
    }

    void ParticleSplatter::Record(const vk::CommandBuffer cmd, const DescriptorAllocator& allocate, const ParticleGpuBuffers& particles,
                                  const ParticleGridBlockGpu& block, const bool fixedPoint)
    {
        auto& impl = *m_Impl;
        const bool useFloat = UsesFloatAtomics(fixedPoint);
        Impl::Variant* splat = nullptr;
        Impl::Variant* resolve = nullptr;
        try
        {
            splat = &impl.Get(0, useFloat);
            resolve = &impl.Get(1, useFloat);
        }
        catch (const std::exception& error)
        {
            impl.Error = std::format("particle splat shader failed to compile: {}", error.what());
            if (impl.Opt.Log) impl.Opt.Log->Error("{}", *impl.Error);
            return;
        }
        if (!particles.PosMass || impl.Current.Resolution <= 1) return;

        const auto accumulator = impl.Current.Accumulator->GetBuffer();
        impl.WriteBlockCommand(cmd, block);
        cmd.fillBuffer(accumulator, 0, vk::WholeSize, 0u);
        impl.Barrier(cmd, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite);

        const auto bind = [&](Impl::Variant& variant)
        {
            const auto set = allocate(variant.Pipeline->GetDescriptorSetLayouts()[0]);
            DescriptorSetWriter writer(impl.Opt.Device->GetDevice());
            const auto buffer = [&](const std::uint32_t binding, const vk::Buffer value)
            {
                if (Impl::Has(variant.Compiled, binding)) writer.WriteStorageBuffer(set, binding, value);
            };
            buffer(0, particles.PosMass);
            buffer(1, particles.VelTemp);
            buffer(2, particles.Meta);
            buffer(3, particles.State);
            buffer(4, accumulator);
            if (Impl::Has(variant.Compiled, 5)) writer.WriteUniformBuffer(set, 5, impl.BlockBuffer->GetBuffer());
            for (std::uint32_t i = 0; i < 4; ++i)
            {
                if (Impl::Has(variant.Compiled, 6 + i)) writer.WriteStorageImage(set, 6 + i, impl.Current.Textures[i]->GetImageView());
            }
            writer.Update();
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, variant.Pipeline->GetPipeline());
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, variant.Pipeline->GetLayout(), 0, set, {});
        };

        bind(*splat);
        cmd.dispatch((particles.Capacity + 63u) / 64u, 1, 1);
        impl.Barrier(cmd, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eShaderWrite);

        bind(*resolve);
        const auto groups = (static_cast<std::uint32_t>(impl.Current.Resolution) + 3u) / 4u;
        for (std::uint32_t level = 0; level < kParticleGridLevels; ++level)
        {
            cmd.pushConstants(resolve->Pipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(level), &level);
            cmd.dispatch(groups, groups, groups);
        }
    }

    struct ParticleSplatStage::CpuUpload
    {
        std::array<std::unique_ptr<VulkanBuffer>, 4> Buffers;
        std::uint32_t Capacity{0};
    };

    namespace
    {
        // Index of the hole in the tracer's heaviest-first list (FillSceneData), -1 when it is not uploaded
        int TracerHoleIndex(const Scene& scene, const std::uint64_t guid)
        {
            std::vector<std::pair<float, std::uint64_t>> holes;
            for (auto [entity, blackHole, transform] :
                 scene.Registry().view<const BlackHoleComponent, const TransformComponent>().each())
            {
                holes.emplace_back(static_cast<float>(blackHole.Mass / Units::kSolarMass), scene.GuidOf(entity));
            }
            std::ranges::stable_sort(holes, std::greater<>{}, &std::pair<float, std::uint64_t>::first);
            for (std::size_t i = 0; i < holes.size() && i < static_cast<std::size_t>(kMaxBlackHoles); ++i)
            {
                if (holes[i].second == guid) return static_cast<int>(i);
            }
            return -1;
        }
    }

    ParticleSplatStage::ParticleSplatStage(RenderResources& resources, ParticleStage& particles)
        : m_Resources(resources), m_Particles(particles)
    {
        ParticleSplatter::Options options;
        options.Device = resources.Device;
        options.FileSystem = resources.FileSystem;
        options.Log = resources.Logger;
        options.ShaderDirectory = resources.FileSystem->ResolveAssetPath("shaders", "particle_splat.comp").parent_path();
        options.CacheDirectory = ".gpp/shader_cache";
        m_Splatter = std::make_unique<ParticleSplatter>(std::move(options));
        m_Cpu = std::make_unique<CpuUpload>();
        resources.ParticleGrid = {false, m_Splatter->Block(), m_Splatter->Textures()};
    }

    ParticleSplatStage::~ParticleSplatStage() = default;

    bool ParticleSplatStage::Ready() const
    {
        return m_Splatter->Ready();
    }

    std::optional<std::string> ParticleSplatStage::Failure() const
    {
        return m_Splatter->Failure();
    }

    void ParticleSplatStage::Record(RenderFrameContext& context)
    {
        m_Splatter->Tick();
        auto& grid = m_Resources.ParticleGrid;
        const auto& data = m_Particles.Data();
        const int hole = data.Active && context.Settings.AccretionDisk ? TracerHoleIndex(context.Scene, data.HoleGuid) : -1;

        if (hole < 0 || !(data.PeakTemperature > 0.0f) || !(data.OuterRadius > 0.0f))
        {
            if (m_BlockActive)
            {
                m_BlockActive = false;
                context.Graph.AddComputePass("Splat", {}, {}, [splatter = m_Splatter.get()](const vk::CommandBuffer cmd, RenderGraph&)
                {
                    splatter->RecordBlock(cmd, ParticleGridBlockGpu{});
                });
            }
            if (++m_IdleFrames > 300 && m_Splatter->Resolution() > 1)
            {
                m_Splatter->Configure(1);
                m_Cpu = std::make_unique<CpuUpload>();
            }
            grid = {false, m_Splatter->Block(), m_Splatter->Textures()};
            return;
        }
        m_IdleFrames = 0;

        const auto layout = MakeParticleGridLayout(data.OuterRadius, context.Settings.ParticleGridResolution);
        m_Splatter->Configure(layout.Resolution);

        const auto& stats = m_Particles.Module().Stats();
        const double meanMass = stats.Particles > 0 ? stats.Mass / static_cast<double>(stats.Particles) : static_cast<double>(data.ReferenceMass);
        const bool fixedPoint = context.Inputs.ExactParticles;
        const auto block = MakeParticleGridBlock(layout, data.Opacity * context.Settings.ParticleOpacityScale, data.PeakTemperature,
                                                 hole, data.LengthM, MakeSplatFixedPoint(meanMass), fixedPoint || !m_Splatter->UsesFloatAtomics(false));
        m_BlockActive = true;
        grid = {true, m_Splatter->Block(), m_Splatter->Textures()};

        const bool gpu = m_Particles.Module().UsesGpu();
        if (!gpu)
        {
            ParticleArrays arrays;
            m_Particles.Module().System()->Backend().Download(arrays);
            const auto count = static_cast<std::uint32_t>(arrays.Size());
            const std::uint32_t capacity = std::max(count, 1u);
            if (m_Cpu->Capacity < capacity)
            {
                for (auto& buffer : m_Cpu->Buffers)
                {
                    buffer = std::make_unique<VulkanBuffer>(m_Resources.Device, MakeStorageBufferSpecification(capacity * sizeof(glm::vec4) * 2u, true), m_Resources.Logger);
                }
                m_Cpu->Capacity = capacity;
            }
            if (count > 0)
            {
                m_Cpu->Buffers[0]->Upload(arrays.PosMass.data(), count * sizeof(glm::vec4));
                m_Cpu->Buffers[1]->Upload(arrays.VelTemp.data(), count * sizeof(glm::vec4));
                m_Cpu->Buffers[2]->Upload(arrays.Meta.data(), count * sizeof(glm::uvec4));
            }
            const std::uint32_t state[4]{count, 0, 0, 0};
            m_Cpu->Buffers[3]->Upload(state, sizeof(state));
        }

        context.Graph.AddComputePass(
            "Splat", {}, {},
            [this, block, textures = m_Splatter->Textures(), fixed = fixedPoint, gpu, count = m_Cpu->Capacity](const vk::CommandBuffer cmd, RenderGraph& graph)
            {
                ParticleGpuBuffers buffers;
                if (gpu) buffers = m_Particles.Buffers();
                else
                {
                    buffers.PosMass = m_Cpu->Buffers[0]->GetBuffer();
                    buffers.VelTemp = m_Cpu->Buffers[1]->GetBuffer();
                    buffers.Meta = m_Cpu->Buffers[2]->GetBuffer();
                    buffers.State = m_Cpu->Buffers[3]->GetBuffer();
                    buffers.Capacity = count;
                }
                m_Splatter->Record(cmd, [&graph](const vk::DescriptorSetLayout layout) { return graph.AllocateDescriptorSet(layout); },
                                   buffers, block, fixed);
            });
    }
}
