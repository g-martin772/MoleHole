export module MoleHole:Rendering.ParticleStage;

import std;
import vulkan;
import glm;
import GPP;
import :Simulation.Components;
import :Simulation.AccretionDisk;
import :Rendering.Pipeline;

export namespace MoleHole
{
    using DiskCommandSink = std::function<void(std::vector<GPP::Command>, GPP::CommandOptions)>;

    // What the render stages after ParticleStage read. Buffers come from ParticleStage::Buffers(), which must be
    // called while recording (it follows the backend's double buffer): binding 0 PosMass (xyz position in
    // Length, w mass kg), 1 VelTemp (xyz velocity in Length/Time, w temperature K), 2 Meta (y bit 0 = alive),
    // 3 State (word 0 = particles stored, alive ones first).
    struct ParticleRenderData
    {
        bool Active{false};
        std::uint32_t Capacity{0};
        glm::dvec3 OriginM{0.0};   // hole position, the particle frame origin
        double LengthM{1.0};       // metres per simulation length (r_g of the epoch)
        double TimeS{1.0};         // seconds per simulation time
        glm::vec3 Normal{0.0f, 1.0f, 0.0f};
        float AspectRatio{0.0f};   // H / R of the viscous layer
        float InnerRadius{0.0f};   // simulation lengths
        float OuterRadius{0.0f};
        float ViewRadius{0.0f};    // half width of the coarse grids, 0 = 1.6 outer radii
        float SchwarzschildRadius{2.0f};
        float Opacity{0.0f};       // m^2/kg
        float PeakTemperature{0.0f}; // K, maximum of the Novikov-Thorne profile of the disk: the colour/brightness reference
        float ReferenceMass{0.0f}; // kg, heaviest emitter weight
        std::uint64_t Epoch{0};
        double SimTime{0.0};       // particle clock, seconds
        std::uint64_t HoleGuid{0};
    };

    // Owns the particle system of the heaviest hole that has an AccretionDisk in Particles mode. Render thread only.
    class ParticleDiskModule
    {
    public:
        using BackendFactory = std::function<std::unique_ptr<GPP::IParticleBackend>(const GPP::ParticleSettings&)>;
        // Runs the recorder in a command buffer and waits for it
        using SubmitFunction = std::function<void(const std::function<void(vk::CommandBuffer)>&)>;

        static constexpr std::uint32_t kInteractiveMacroSteps = 48;
        static constexpr double kBaseStepGeometric = 8.0; // macro step in units of GM/c^3
        // Exports: the block-step scheduler subdivides for fast particles, the outer disk does not need 8
        static constexpr double kExportBaseStepGeometric = 8.0;
        static constexpr std::uint32_t kBatchMacroSteps = 240; // per command buffer of a synchronous advance (ring holds 256)

        ParticleDiskModule(AccretionBoard& board, std::shared_ptr<GPP::ParticleHub> hub, BackendFactory factory = {},
                           std::shared_ptr<GPP::Logger> logger = nullptr);

        // Reads the snapshot, (re)creates the system and decides how far the clock moved. True when a disk is active.
        bool Prepare(const GPP::Scene& scene, const DiskCommandSink& sink, bool unlimited);
        // Advances by the planned time. GPU backends record into `commandBuffer`; CPU backends ignore it.
        void Advance(vk::CommandBuffer commandBuffer, std::uint32_t frameSlot);
        // CPU backend, exports and tests: the same without a command buffer (a GPU backend submits every step itself)
        void AdvanceNow();

        void SetSubmit(SubmitFunction submit) { m_Submit = std::move(submit); }
        [[nodiscard]] bool Active() const noexcept { return m_Active; }
        [[nodiscard]] bool UsesGpu() const noexcept { return m_Gpu != nullptr; }
        [[nodiscard]] const ParticleRenderData& Data() const noexcept { return m_Data; }
        [[nodiscard]] GPP::ParticleSystem* System() noexcept { return m_System.get(); }
        [[nodiscard]] GPP::GpuParticleBackend* Gpu() const noexcept { return m_Gpu; }
        [[nodiscard]] double PlannedSeconds() const noexcept { return m_Planned; }
        [[nodiscard]] double LastCpuMs() const noexcept { return m_CpuMs; }
        [[nodiscard]] const DiskStats& Stats() const noexcept { return m_Stats; }
        [[nodiscard]] std::uint32_t Slots() const noexcept { return kSlots; }
        void CollectSlot(std::uint32_t slot);

    private:
        static constexpr std::uint32_t kSlots = 3;

        void Rebuild(const AccretionDiskComponent& disk, double referenceWeight);
        void Deactivate();
        void UpdateBoard();
        void ApplyFeedback();
        void RunAdvance(bool synchronous);

        AccretionBoard& m_Board;
        std::shared_ptr<GPP::ParticleHub> m_Hub;
        BackendFactory m_Factory;
        SubmitFunction m_Submit;
        std::shared_ptr<GPP::Logger> m_Logger;

        std::unique_ptr<GPP::ParticleSystem> m_System;
        GPP::GpuParticleBackend* m_Gpu{nullptr};
        bool m_Active{false};
        int m_IdleFrames{0};
        std::uint32_t m_Capacity{0};
        double m_ReferenceWeight{0.0};

        std::uint64_t m_Guid{0};
        std::uint64_t m_Epoch{0};
        std::uint64_t m_StateEpoch{std::numeric_limits<std::uint64_t>::max()};
        std::uint64_t m_RingKey{0};
        GPP::ParticleUnits m_Units;
        double m_LastTime{0.0};
        double m_TargetTime{0.0};
        double m_Planned{0.0};
        bool m_Unlimited{false};
        bool m_NeedPrime{false};
        bool m_Advanced{false};

        AccretionDiskComponent m_Disk;
        BlackHoleComponent m_Hole;
        DiskStats m_Stats;
        ParticleRenderData m_Data;
        double m_CpuMs{0.0};
        std::uint32_t m_LastMacroSteps{0};
        bool m_LastLimited{false};

        std::deque<std::pair<double, double>> m_History;
        std::chrono::steady_clock::time_point m_LastFeedback{};
        AccretionDelta m_AppliedDelta;
        DiskCommandSink m_Sink;
        double m_WindowSeconds{0.0};
    };

    class ParticleStage final : public IRenderStage
    {
    public:
        explicit ParticleStage(RenderResources& resources);

        [[nodiscard]] std::string_view Name() const override { return "Particles"; }
        void Record(RenderFrameContext& context) override;

        [[nodiscard]] ParticleDiskModule& Module() noexcept { return m_Module; }
        [[nodiscard]] const ParticleRenderData& Data() const noexcept { return m_Module.Data(); }
        // Valid while recording, after this stage's pass ran
        [[nodiscard]] GPP::ParticleGpuBuffers Buffers() const;

    private:
        RenderResources& m_Resources;
        ParticleDiskModule m_Module;
        std::unique_ptr<GPP::VulkanCommandPool> m_Pool;
        std::uint32_t m_Frame{0};
    };

    // Top-down (left) and edge-on (right) density picture of particles in the hole frame, for debugging without a renderer.
    // `extent` is the half width in simulation lengths; returns false when the file cannot be written.
    bool WriteParticleScatter(const std::string& path, const GPP::ParticleArrays& particles, const glm::vec3& normal,
                              float extent, float innerRadius, int size = 480);

    // Runs the particle disk of a scene file headless on the CPU backend and writes scatter pictures at `frames` evenly
    // spaced moments up to `seconds` of particle time (path stem + _<k>.png). Returns a process exit code.
    int RunDebugParticles(const std::string& scenePath, const std::string& pngPath, double seconds, int frames);
}
