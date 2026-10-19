module MoleHole;

import :Rendering.ParticleStage;
import :Rendering.Pipeline;
import :Simulation.AccretionDisk;
import :Simulation.Components;
import std;
import vulkan;
import glm;
import GPP;

using namespace GPP;

namespace MoleHole
{
    namespace
    {
        std::uint64_t Mix(std::uint64_t hash, const std::uint64_t word)
        {
            hash ^= word + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
            return hash;
        }

        void PrintDiskProfile(const ParticleArrays& particles, const ParticleRenderData& data, const DiskStats& stats);

        std::uint64_t RingKey(const AccretionDiskComponent& disk)
        {
            std::uint64_t hash = 1469598103934665603ull;
            hash = Mix(hash, std::bit_cast<std::uint64_t>(disk.RingRadius));
            hash = Mix(hash, std::bit_cast<std::uint64_t>(disk.RingEccentricity));
            hash = Mix(hash, std::bit_cast<std::uint64_t>(disk.RingMass));
            hash = Mix(hash, static_cast<std::uint64_t>(disk.RingParticles));
            hash = Mix(hash, std::bit_cast<std::uint64_t>(disk.TruncationRadius));
            hash = Mix(hash, std::bit_cast<std::uint64_t>(disk.ShockDamping));
            hash = Mix(hash, std::bit_cast<std::uint64_t>(disk.SeedMass));
            hash = Mix(hash, std::bit_cast<std::uint64_t>(disk.SeedInner));
            hash = Mix(hash, std::bit_cast<std::uint64_t>(disk.SeedOuter));
            hash = Mix(hash, static_cast<std::uint64_t>(disk.SeedParticles));
            hash = Mix(hash, static_cast<std::uint64_t>(disk.Seed));
            hash = Mix(hash, static_cast<std::uint64_t>(disk.MaxParticles));
            hash = Mix(hash, static_cast<std::uint64_t>(disk.FeedOuterEdge));
            return hash;
        }
    }

    ParticleDiskModule::ParticleDiskModule(AccretionBoard& board, std::shared_ptr<ParticleHub> hub, BackendFactory factory,
                                           std::shared_ptr<Logger> logger)
        : m_Board(board), m_Hub(std::move(hub)), m_Factory(std::move(factory)), m_Logger(std::move(logger))
    {
    }

    void ParticleDiskModule::Deactivate()
    {
        if (m_Active)
        {
            m_Active = false;
            m_Data = {};
            m_Stats.Active = false;
            m_Board.Publish(m_Guid, m_Stats);
        }
        if (m_System && ++m_IdleFrames > 300)
        {
            m_System.reset();
            m_Gpu = nullptr;
            m_Guid = 0;
        }
    }

    void ParticleDiskModule::Rebuild(const AccretionDiskComponent& disk, const double referenceWeight)
    {
        ParticleSettings settings;
        settings.Units = m_Units;
        settings.Capacity = static_cast<std::uint32_t>(std::max(disk.MaxParticles, 1024));
        settings.MaxBin = 10;
        settings.Eta = 0.005f;
        settings.ReferenceWeight = referenceWeight > 0.0 ? static_cast<float>(referenceWeight) : 1.0f;
        ParticleSystemConfig config;
        config.Settings = settings;
        config.BaseStep = kBaseStepGeometric * m_Units.Time;
        config.MaxMacroSteps = 1u << 20;

        m_System.reset();
        m_Gpu = nullptr;
        if (m_Factory)
        {
            try
            {
                auto backend = m_Factory(settings);
                if (backend)
                {
                    m_System = std::make_unique<ParticleSystem>(std::move(backend), config);
                }
            }
            catch (const std::exception& error)
            {
                if (m_Logger) m_Logger->Warn("Particle disk: GPU backend unavailable ({}), using the CPU backend", error.what());
                m_System.reset();
            }
        }
        if (!m_System)
        {
            config.Settings.Capacity = std::min(config.Settings.Capacity, 1u << 17);
            m_System = std::make_unique<ParticleSystem>(std::make_unique<CpuParticleBackend>(), config);
        }
        m_System->SetHooks(MakeDiskHooks());
        m_System->SetHub(m_Hub);
        m_Gpu = dynamic_cast<GpuParticleBackend*>(&m_System->Backend());
        m_Capacity = settings.Capacity;
        m_ReferenceWeight = settings.ReferenceWeight;
        m_StateEpoch = std::numeric_limits<std::uint64_t>::max();
    }

    bool ParticleDiskModule::Prepare(const Scene& scene, const DiskCommandSink& sink, const bool unlimited)
    {
        const BlackHoleComponent* hole = nullptr;
        const AccretionDiskComponent* disk = nullptr;
        const TransformComponent* transform = nullptr;
        std::uint64_t guid = 0;
        for (auto [entity, candidate, component, placement] :
             scene.Registry().view<const BlackHoleComponent, const AccretionDiskComponent, const TransformComponent>().each())
        {
            if (component.Model != DiskModel::Particles || !(candidate.Mass > 0.0)) continue;
            if (hole && candidate.Mass <= hole->Mass) continue;
            hole = &candidate;
            disk = &component;
            transform = &placement;
            guid = scene.GuidOf(entity);
        }
        if (!hole)
        {
            Deactivate();
            return false;
        }

        m_Disk = *disk;
        m_Hole = *hole;
        m_Sink = sink;
        m_Unlimited = unlimited;
        const double now = scene.Clock().Time;
        const std::uint64_t ringKey = RingKey(*disk);
        const bool wasActive = m_Active;
        const bool backwards = m_System && wasActive && now < m_LastTime - 1e-9 * std::max(1.0, std::abs(m_LastTime));
        const bool reset = !m_System || !wasActive || guid != m_Guid || ringKey != m_RingKey || backwards;
        m_IdleFrames = 0;
        m_Active = true;

        if (reset)
        {
            if (m_System && wasActive && (backwards || guid != m_Guid) && m_Hub) m_Hub->Reset();
            ++m_Epoch;
            m_Units = ParticleUnits::Geometrized(hole->Mass);
            m_Guid = guid;
            m_RingKey = ringKey;
            m_LastTime = unlimited ? 0.0 : now;
            m_History.clear();
            m_AppliedDelta = {};
            m_NeedPrime = true;
        }

        const DiskSetup setup = MakeDiskSetup(*disk, *hole, m_Units);
        const glm::dvec3 position = transform->Position;
        auto ring = MakeDiskRing(*disk, *hole, setup, m_Units, position);
        auto feed = MakeDiskFeed(*disk, *hole, setup, m_Units, position);

        const double drift = DiskDriftTime(setup) * m_Units.Time;
        m_WindowSeconds = std::max(5000.0 * m_Units.Time, 0.02 * drift);

        const ParticleFrame frame{position, m_Units};
        const auto snapshot = m_Hub->Snapshot();
        auto state = GatherParticleScene(scene, frame, m_Epoch, 0, &snapshot);
        state.Sinks.push_back(MakeDiskSink(*hole, position));
        if (ring) state.Emitters.push_back(std::move(*ring));
        for (auto& seed : MakeDiskSeed(*disk, *hole, setup, m_Units, position)) state.Emitters.push_back(std::move(seed));
        if (feed) state.Emitters.push_back(std::move(*feed));
        if (!m_System || m_Capacity != static_cast<std::uint32_t>(std::max(disk->MaxParticles, 1024)))
        {
            double weight = 0.0;
            for (const auto& emitter : state.Emitters)
            {
                weight = std::max(weight, ParticleSystem::RateAndWeight(emitter.Params).second);
            }
            Rebuild(*disk, weight);
            m_NeedPrime = true;
        }
        if (state.Epoch != m_StateEpoch)
        {
            m_StateEpoch = state.Epoch;
            m_NeedPrime = true;
        }

        if (reset || m_NeedPrime) m_System->SetBaseStep((unlimited ? kExportBaseStepGeometric : kBaseStepGeometric) * m_Units.Time);
        m_System->Consume(state);
        m_System->SetForce(setup.Force);
        if (m_NeedPrime)
        {
            m_System->Prime();
            m_NeedPrime = false;
            m_LastTime = unlimited ? 0.0 : now;
            m_History.clear();
            m_AppliedDelta = {};
            UpdateBoard();
        }

        m_TargetTime = now;
        m_Planned = std::max(now - m_LastTime, 0.0);
        m_LastLimited = false;
        if (!m_Unlimited)
        {
            const double cap = kInteractiveMacroSteps * kBaseStepGeometric * m_Units.Time;
            if (m_Planned > cap)
            {
                m_Planned = cap;
                m_LastLimited = true;
            }
        }

        m_Data.Active = true;
        m_Data.Capacity = m_Capacity;
        m_Data.OriginM = position;
        m_Data.LengthM = m_Units.Length;
        m_Data.TimeS = m_Units.Time;
        m_Data.Normal = glm::vec3(setup.Normal);
        m_Data.AspectRatio = static_cast<float>(setup.AspectRatio);
        m_Data.InnerRadius = static_cast<float>(setup.InnerRadius);
        m_Data.OuterRadius = static_cast<float>(setup.OuterRadius);
        m_Data.ViewRadius = disk->ViewRadius > 0.0 ? static_cast<float>(disk->ViewRadius * Units::GravitationalRadius(hole->Mass) / m_Units.Length) : 0.0f;
        m_Data.SchwarzschildRadius = setup.Force.SchwarzschildRadius;
        m_Data.Opacity = static_cast<float>(disk->Opacity);
        m_Data.PeakTemperature = static_cast<float>(DiskPeakTemperature(setup));
        m_Data.ReferenceMass = static_cast<float>(m_ReferenceWeight);
        m_Data.Epoch = state.Epoch;
        m_Data.HoleGuid = guid;
        return true;
    }

    void ParticleDiskModule::RunAdvance(const bool synchronous)
    {
        const auto start = std::chrono::steady_clock::now();
        m_LastMacroSteps = 0;
        if (m_Planned > 0.0 && synchronous && m_Gpu && m_Submit)
        {
            // Many macro steps per submission: one fence per step would dominate a long advance
            const double chunk = kBatchMacroSteps * m_System->Config().BaseStep;
            double remaining = m_Planned;
            while (remaining > 0.0)
            {
                const double seconds = std::min(remaining, chunk);
                m_Submit([&](const vk::CommandBuffer cmd)
                {
                    m_Gpu->BeginRecording(cmd, 0);
                    const auto report = m_System->Advance(seconds);
                    m_LastMacroSteps += report.MacroSteps;
                    m_Gpu->EndRecording();
                });
                m_Gpu->CollectCompleted(0);
                remaining -= seconds;
            }
        }
        else if (m_Planned > 0.0)
        {
            const auto report = m_System->Advance(m_Planned);
            m_LastMacroSteps = report.MacroSteps;
        }
        m_LastTime = m_TargetTime;
        m_Data.SimTime = m_System->SimTime();
        m_CpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (!m_Gpu || synchronous)
        {
            m_System->Refresh();
            UpdateBoard();
        }
    }

    void ParticleDiskModule::Advance(const vk::CommandBuffer commandBuffer, const std::uint32_t frameSlot)
    {
        if (!m_Active || !m_System) return;
        if (m_Gpu) m_Gpu->BeginRecording(commandBuffer, frameSlot);
        RunAdvance(false);
        if (m_Gpu) m_Gpu->EndRecording();
    }

    void ParticleDiskModule::AdvanceNow()
    {
        if (!m_Active || !m_System) return;
        RunAdvance(true);
    }

    void ParticleDiskModule::CollectSlot(const std::uint32_t slot)
    {
        if (!m_Gpu || !m_System) return;
        m_Gpu->CollectCompleted(slot);
        m_System->Refresh();
        UpdateBoard();
    }

    void ParticleDiskModule::UpdateBoard()
    {
        const auto data = m_System->Board()->Latest();
        if (!data) return;
        DiskStats stats;
        stats.Active = true;
        stats.Gpu = m_Gpu != nullptr;
        stats.Particles = data->Stats.Alive;
        stats.Mass = data->Stats.AliveMass;
        stats.MeanTemperature = data->Stats.MeanTemperature;
        stats.Dropped = data->Stats.Dropped;
        stats.SimTime = data->Stats.SimTime;
        stats.MacroSteps = m_LastMacroSteps;
        stats.RecordMs = m_CpuMs;
        stats.Limited = m_LastLimited;
        const double inner = m_Disk.InnerRadius > 0.0 ? m_Disk.InnerRadius : 6.0;
        stats.InnerRadius = inner * Units::GravitationalRadius(m_Hole.Mass);
        if (const auto sink = m_System->Board()->Sink(kDiskSinkId))
        {
            stats.AbsorbedMass = sink->Mass();
            stats.AbsorbedEnergy = sink->Energy(m_Units);
            stats.AbsorbedAngularMomentum = sink->AngularMomentum(m_Units);
        }

        m_History.emplace_back(stats.SimTime, stats.AbsorbedMass);
        while (m_History.size() > 2 && m_History[1].first <= stats.SimTime - m_WindowSeconds) m_History.pop_front();
        const auto& oldest = m_History.front();
        const double span = stats.SimTime - oldest.first;
        stats.MassRate = span > 0.25 * m_WindowSeconds ? (stats.AbsorbedMass - oldest.second) / span : 0.0;

        m_Stats = stats;
        m_Board.Publish(m_Guid, stats);
        ApplyFeedback();
    }

    void ParticleDiskModule::ApplyFeedback()
    {
        if (!m_Disk.FeedBackToHole || !m_Sink) return;
        const auto now = std::chrono::steady_clock::now();
        if (now - m_LastFeedback < std::chrono::milliseconds(250)) return;
        const auto sink = m_System->Board()->Sink(kDiskSinkId);
        if (!sink) return;
        glm::dvec3 axis(m_Hole.SpinAxis);
        axis = glm::length(axis) > 0.0 ? glm::normalize(axis) : glm::dvec3(0.0, 1.0, 0.0);
        const AccretionDelta total = AccretionDeltaOf(*sink, m_Units, axis);
        const AccretionDelta delta{total.RestMass - m_AppliedDelta.RestMass, total.Energy - m_AppliedDelta.Energy,
                                   total.AngularMomentum - m_AppliedDelta.AngularMomentum};
        if (!(delta.RestMass > 0.0)) return;
        const HoleUpdate update = ApplyAccretion(m_Hole.Mass, m_Hole.Spin, delta);
        m_AppliedDelta = total;
        m_LastFeedback = now;
        std::vector<Command> commands;
        commands.push_back(SetFieldCommand{m_Guid, "BlackHole", "Mass", FieldValue{update.Mass}});
        commands.push_back(SetFieldCommand{m_Guid, "BlackHole", "Spin", FieldValue{update.Spin}});
        CommandOptions options;
        options.Undoable = true;
        options.Label = "Accretion";
        options.CoalesceKey = "accretion";
        options.OnlyWhilePlaying = true;
        m_Sink(std::move(commands), std::move(options));
    }

    ParticleStage::ParticleStage(RenderResources& resources)
        : m_Resources(resources),
          m_Module(
              GlobalAccretionBoard(), GlobalParticleHub(),
              [&resources](const ParticleSettings&) -> std::unique_ptr<IParticleBackend>
              {
                  if (!resources.Device) return nullptr;
                  GpuParticleOptions options;
                  options.Device = resources.Device;
                  options.FileSystem = resources.FileSystem;
                  options.Log = resources.Logger;
                  for (const char* candidate : {"dependencies/gpp/shaders/particles", "../gpp/shaders/particles"})
                  {
                      const auto path = resources.FileSystem->ResolvePath(candidate);
                      if (std::filesystem::exists(path / "step.comp"))
                      {
                          options.ShaderDirectory = path;
                          break;
                      }
                  }
                  if (options.ShaderDirectory.empty()) throw std::runtime_error("gpp particle shaders not found");
                  options.IncludeDirectories.push_back(resources.FileSystem->ResolveAssetPath("shaders", ""));
                  options.Force = ParticleForceKind::Custom;
                  options.PostMacroStep = true;
                  options.FramesInFlight = 3;
                  options.CacheDirectory = ".gpp/shader_cache";
                  return std::make_unique<GpuParticleBackend>(std::move(options));
              },
              resources.Logger)
    {
    }

    void ParticleStage::Record(RenderFrameContext& context)
    {
        if (!m_Pool && m_Resources.Device)
        {
            m_Pool = std::make_unique<VulkanCommandPool>(m_Resources.Device, m_Resources.Logger,
                                                         m_Resources.Device->GetQueueIndices().Graphics);
            m_Module.SetSubmit([this](const std::function<void(vk::CommandBuffer)>& record)
            {
                ImmediateSubmit(*m_Pool, m_Resources.Device->GetGraphicsQueue(), record);
            });
        }
        if (!m_Module.Prepare(context.Scene, context.Inputs.EnqueueDiskCommands, context.Inputs.ExactParticles)) return;
        if (!m_Module.UsesGpu() || context.Inputs.ExactParticles)
        {
            m_Module.AdvanceNow();
            if (context.Inputs.ExactParticles && m_Module.System() && std::getenv("MOLEHOLE_DISK_PROFILE"))
            {
                ParticleArrays particles;
                m_Module.System()->Backend().Download(particles);
                PrintDiskProfile(particles, m_Module.Data(), m_Module.Stats());
            }
            return;
        }
        const std::uint32_t slot = m_Frame++ % m_Module.Slots();
        m_Module.CollectSlot((slot + 1) % m_Module.Slots());
        context.Graph.AddComputePass("Particles", {}, {},
                                     [module = &m_Module, slot](const vk::CommandBuffer cmd, RenderGraph&)
                                     {
                                         module->Advance(cmd, slot);
                                     });
    }

    ParticleGpuBuffers ParticleStage::Buffers() const
    {
        return m_Module.Gpu() ? m_Module.Gpu()->Buffers() : ParticleGpuBuffers{};
    }

    bool WriteParticleScatter(const std::string& path, const ParticleArrays& particles, const glm::vec3& normal,
                              const float extent, const float innerRadius, const int size)
    {
        const glm::vec3 helper = std::abs(normal.z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 u = glm::normalize(glm::cross(helper, normal));
        const glm::vec3 v = glm::cross(normal, u);
        const int width = size * 2;
        std::vector<float> density(static_cast<std::size_t>(width) * size, 0.0f);
        const auto splat = [&](const int panel, const float a, const float b)
        {
            const int x = static_cast<int>((a / extent * 0.5f + 0.5f) * static_cast<float>(size));
            const int y = static_cast<int>((0.5f - b / extent * 0.5f) * static_cast<float>(size));
            if (x < 0 || y < 0 || x >= size || y >= size) return;
            density[static_cast<std::size_t>(y) * width + panel * size + x] += 1.0f;
        };
        for (std::size_t i = 0; i < particles.Size(); ++i)
        {
            const glm::vec3 p(particles.PosMass[i]);
            splat(0, glm::dot(p, u), glm::dot(p, v));
            splat(1, glm::dot(p, u), glm::dot(p, normal) * 8.0f);
        }
        float peak = 1.0f;
        for (const float d : density) peak = std::max(peak, d);
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * size * 3, 0);
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const float t = std::log1p(density[static_cast<std::size_t>(y) * width + x]) / std::log1p(peak);
                auto* out = &pixels[(static_cast<std::size_t>(y) * width + x) * 3];
                out[0] = static_cast<std::uint8_t>(255.0f * std::clamp(2.0f * t, 0.0f, 1.0f));
                out[1] = static_cast<std::uint8_t>(255.0f * std::clamp(2.0f * t - 0.6f, 0.0f, 1.0f));
                out[2] = static_cast<std::uint8_t>(255.0f * std::clamp(3.0f * t - 2.0f, 0.0f, 1.0f));
            }
        }
        const auto mark = [&](const int panel, const float radius)
        {
            for (int k = 0; k < 720; ++k)
            {
                const float angle = static_cast<float>(k) * 6.2831853f / 720.0f;
                const int x = static_cast<int>((radius * std::cos(angle) / extent * 0.5f + 0.5f) * static_cast<float>(size));
                const int y = static_cast<int>((0.5f - radius * std::sin(angle) / extent * 0.5f) * static_cast<float>(size));
                if (x < 0 || y < 0 || x >= size || y >= size) continue;
                auto* out = &pixels[(static_cast<std::size_t>(y) * width + panel * size + x) * 3];
                out[1] = std::max<std::uint8_t>(out[1], 90);
                out[2] = std::max<std::uint8_t>(out[2], 160);
            }
        };
        if (innerRadius > 0.0f) mark(0, innerRadius);
        return stbi_write_png(path.c_str(), width, size, 3, pixels.data(), width * 3) != 0;
    }

    namespace
    {
        // Cylindrical radial profile of the particle disk in the hole frame: counts, column density, vertical optical
        // depth to the mid-plane, mass weighted temperature, rms height over radius, mean speed
        void PrintDiskProfile(const ParticleArrays& particles, const ParticleRenderData& data, const DiskStats& stats)
        {
            std::println("stats: alive {} mass {:.4g} kg, absorbed {:.4g} kg, measured mdot {:.4g} kg/s, dropped {}, sim {:.4g} s, limited {}",
                         stats.Particles, stats.Mass, stats.AbsorbedMass, stats.MassRate, stats.Dropped, stats.SimTime, stats.Limited);
            const double inner = std::max<double>(data.InnerRadius, 1.0);
            const double outer = std::max<double>(data.OuterRadius, inner * 2.0);
            constexpr int kBins = 14;
            const double lo = inner * 0.7;
            const double hi = outer * 1.6;
            struct Bin { double Count{0}, Mass{0}, MassT{0}, Z2{0}, Speed{0}; };
            std::array<Bin, kBins> bins{};
            double massSum = 0.0;
            std::size_t alive = 0;
            const glm::vec3 normal = data.Normal;
            for (std::size_t i = 0; i < particles.Size(); ++i)
            {
                if (!ParticleArrays::Alive(particles.Meta[i])) continue;
                ++alive;
                const glm::vec3 p(particles.PosMass[i]);
                const double mass = particles.PosMass[i].w;
                massSum += mass;
                const float z = glm::dot(p, normal);
                const double R = glm::length(p - z * normal);
                if (R < lo || R >= hi) continue;
                const int b = static_cast<int>(std::log(R / lo) / std::log(hi / lo) * kBins);
                auto& bin = bins[static_cast<std::size_t>(std::clamp(b, 0, kBins - 1))];
                bin.Count += 1.0;
                bin.Mass += mass;
                bin.MassT += mass * particles.VelTemp[i].w;
                bin.Z2 += mass * z * z / (R * R);
                bin.Speed += mass * glm::length(glm::vec3(particles.VelTemp[i]));
            }
            std::println("particles alive {} mean mass {:.4g} kg, r_g = {:.5g} m, kappa = {:.3g} m2/kg, T_ref = {:.4g} K",
                         alive, alive ? massSum / static_cast<double>(alive) : 0.0, data.LengthM, data.Opacity, data.PeakTemperature);
            std::println("   R [rg]      N   Sigma [kg/m2]  tau_half   T [K]   h_rms   v/c   N per rg2");
            for (int b = 0; b < kBins; ++b)
            {
                const auto& bin = bins[static_cast<std::size_t>(b)];
                if (bin.Count == 0) continue;
                const double r0 = lo * std::pow(hi / lo, static_cast<double>(b) / kBins);
                const double r1 = lo * std::pow(hi / lo, static_cast<double>(b + 1) / kBins);
                const double area = std::numbers::pi * (r1 * r1 - r0 * r0);
                const double sigma = bin.Mass / (area * data.LengthM * data.LengthM);
                std::println("{:6.1f}-{:5.1f} {:7.0f} {:12.4g} {:10.3g} {:9.4g} {:6.3f} {:6.3f} {:8.1f}", r0, r1, bin.Count, sigma,
                             0.5 * sigma * data.Opacity, bin.MassT / bin.Mass, std::sqrt(bin.Z2 / bin.Mass),
                             bin.Speed / bin.Mass, bin.Count / area);
            }
        }
    }

    int RunDebugParticles(const std::string& scenePath, const std::string& pngPath, const double seconds, const int frames)
    {
        std::ifstream file(scenePath);
        if (!file)
        {
            std::println(std::cerr, "debug-particles: cannot open {}", scenePath);
            return 2;
        }
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        Scene scene;
        scene.DeserializeFromYaml(text);

        AccretionBoard board;
        ParticleDiskModule module(board, std::make_shared<ParticleHub>());
        if (!module.Prepare(scene, {}, true))
        {
            std::println(std::cerr, "debug-particles: no hole with an AccretionDisk in Particles mode");
            return 3;
        }
        const std::filesystem::path base(pngPath);
        const int count = std::max(frames, 1);
        for (int k = 0; k <= count; ++k)
        {
            if (k > 0)
            {
                scene.SetSimTime(seconds * static_cast<double>(k) / count);
                module.Prepare(scene, {}, true);
                module.AdvanceNow();
            }
            ParticleArrays particles;
            module.System()->Backend().Download(particles);
            const auto& data = module.Data();
            const auto& stats = module.Stats();
            std::vector<float> radii;
            radii.reserve(particles.Size());
            for (std::size_t i = 0; i < particles.Size(); ++i) radii.push_back(glm::length(glm::vec3(particles.PosMass[i])));
            std::ranges::sort(radii);
            const float reach = radii.empty() ? 0.0f : radii[static_cast<std::size_t>(0.97 * static_cast<double>(radii.size() - 1))];
            const float extent = std::max(1.3f * reach, 4.0f * data.SchwarzschildRadius);
            const auto name = base.parent_path() / std::format("{}_{}{}", base.stem().string(), k, base.extension().string());
            const bool ok = WriteParticleScatter(name.string(), particles, data.Normal, extent, data.InnerRadius);
            std::println("frame {} t={:.4g} s particles={} mass={:.4g} kg absorbed={:.4g} kg mdot={:.4g} kg/s T={:.4g} K {}",
                         k, seconds * k / count, stats.Particles, stats.Mass, stats.AbsorbedMass, stats.MassRate,
                         stats.MeanTemperature, ok ? name.string() : "write failed");
            if (k == count) PrintDiskProfile(particles, data, module.Stats());
            if (k == count && std::getenv("MOLEHOLE_DUMP"))
            {
                std::ofstream dump(std::getenv("MOLEHOLE_DUMP"));
                for (std::size_t i = 0; i < particles.Size(); ++i)
                {
                    if (!ParticleArrays::Alive(particles.Meta[i])) continue;
                    dump << particles.PosMass[i].x << ' ' << particles.PosMass[i].y << ' ' << particles.PosMass[i].z << ' '
                         << particles.VelTemp[i].x << ' ' << particles.VelTemp[i].y << ' ' << particles.VelTemp[i].z << ' '
                         << particles.VelTemp[i].w << ' ' << particles.Meta[i].w << '\n';
                }
            }
        }
        return 0;
    }
}
