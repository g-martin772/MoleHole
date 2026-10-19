export module MoleHole:Rendering.ParticleSplat;

import std;
import vulkan;
import glm;
import GPP;
import :Simulation.AccretionDisk;
import :Rendering.Pipeline;
import :Rendering.ParticleStage;

export namespace MoleHole
{
    inline constexpr int kParticleGridLevels = 2;
    inline constexpr int kParticleGridChannels = 5; // m, m v_x, m v_y, m v_z, m T / T_ref
    inline constexpr int kParticleTemperatureUnits = 4;
    // Half widths of the coarse and fine level in outer radii of the disk
    inline constexpr double kParticleGridCoarseHalf = 1.6;
    inline constexpr double kParticleGridFineHalf = 0.32;

    struct ParticleGridLevel
    {
        glm::vec3 Min{0.0f}; // minimum corner in r_g of the hole, axes of the world
        float Voxel{1.0f};   // r_g
    };

    struct ParticleGridLayout
    {
        int Resolution{128};
        std::array<ParticleGridLevel, kParticleGridLevels> Levels; // 0 fine, 1 coarse

        [[nodiscard]] float Half(const int level) const noexcept
        {
            return 0.5f * static_cast<float>(Resolution) * Levels[static_cast<std::size_t>(level)].Voxel;
        }
        [[nodiscard]] std::size_t NodeCount() const noexcept
        {
            const auto n = static_cast<std::size_t>(Resolution);
            return n * n * n;
        }
    };

    [[nodiscard]] ParticleGridLayout MakeParticleGridLayout(double outerRadius, int resolution);

    // std140 block shared with shaders/particle_grid.glsl
    struct ParticleGridBlockGpu
    {
        glm::vec4 Min[kParticleGridLevels]{};
        glm::vec4 Info{0.0f, 0.0f, 0.0f, -1.0f};  // voxels per axis, kappa, reference temperature, tracer hole index
        glm::vec4 Frame{0.0f};                    // r_g in metres, fixed-point units per kg (0 = float atomics), kg per unit
    };
    static_assert(sizeof(ParticleGridBlockGpu) == 64);

    struct SplatFixedPoint
    {
        float InverseUnit{1.0f}; // units per kg
        float Unit{1.0f};        // kg per unit
    };

    // 2^-12 of the mean particle mass per unit: one voxel can hold 2^19 mean particles
    [[nodiscard]] SplatFixedPoint MakeSplatFixedPoint(double meanMass);

    [[nodiscard]] ParticleGridBlockGpu MakeParticleGridBlock(const ParticleGridLayout& layout, float kappa,
                                                             float referenceTemperature, int holeIndex, double lengthM,
                                                             const SplatFixedPoint& fixed, bool fixedPoint);

    // The splat on the CPU: kParticleGridLevels * N^3 * 5 values in the layout of the shader's accumulator, decoded to
    // kg. With `fixed` it quantises like the shader's integer atomics.
    [[nodiscard]] std::vector<float> SplatReference(const GPP::ParticleArrays& particles, const ParticleGridLayout& layout,
                                                    float referenceTemperature, const SplatFixedPoint* fixed);

    // Column density D = rho r_g [kg/m^2] of a node holding `mass` kg
    [[nodiscard]] inline double ParticleColumnDensity(const double mass, const float voxel, const double lengthM)
    {
        const double v = voxel;
        return mass / (v * v * v * lengthM * lengthM);
    }

    class ParticleSplatter
    {
    public:
        struct Options
        {
            std::shared_ptr<GPP::VulkanDevice> Device;
            std::shared_ptr<GPP::IFileSystem> FileSystem;
            std::shared_ptr<GPP::Logger> Log;
            std::filesystem::path ShaderDirectory;
            std::filesystem::path CacheDirectory;
            bool PreferAtomicFloat{true};
        };

        using DescriptorAllocator = std::function<vk::DescriptorSet(vk::DescriptorSetLayout)>;

        explicit ParticleSplatter(Options options);
        ~ParticleSplatter();
        ParticleSplatter(const ParticleSplatter&) = delete;
        ParticleSplatter& operator=(const ParticleSplatter&) = delete;

        // Allocates the accumulators and textures for N voxels per axis (1 = placeholders); a no-op when unchanged
        void Configure(int resolution);
        // Call once per frame: releases resources replaced by Configure once the GPU cannot use them any more
        void Tick();

        // Clear, splat, resolve. `fixedPoint` selects the order independent integer accumulation (always used without float atomics)
        void Record(vk::CommandBuffer commandBuffer, const DescriptorAllocator& allocate, const GPP::ParticleGpuBuffers& particles,
                    const ParticleGridBlockGpu& block, bool fixedPoint);
        // Writes the block alone (when no disk is drawn any more)
        void RecordBlock(vk::CommandBuffer commandBuffer, const ParticleGridBlockGpu& block);
        // Writes the block outside of a recording
        void WriteBlock(const ParticleGridBlockGpu& block);

        [[nodiscard]] int Resolution() const noexcept;
        [[nodiscard]] bool UsesFloatAtomics(bool fixedPoint) const noexcept;
        [[nodiscard]] vk::Buffer Block() const;
        [[nodiscard]] vk::Buffer Accumulator() const;
        [[nodiscard]] std::array<std::shared_ptr<GPP::VulkanImage>, 4> Textures() const;
        [[nodiscard]] bool Ready() const noexcept;
        [[nodiscard]] std::optional<std::string> Failure() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class ParticleSplatStage final : public IRenderStage
    {
    public:
        ParticleSplatStage(RenderResources& resources, ParticleStage& particles);
        ~ParticleSplatStage() override;

        [[nodiscard]] std::string_view Name() const override { return "Splat"; }
        [[nodiscard]] bool Ready() const override;
        [[nodiscard]] std::optional<std::string> Failure() const override;
        void Record(RenderFrameContext& context) override;

        [[nodiscard]] ParticleSplatter& Splatter() noexcept { return *m_Splatter; }

    private:
        struct CpuUpload;

        RenderResources& m_Resources;
        ParticleStage& m_Particles;
        std::unique_ptr<ParticleSplatter> m_Splatter;
        std::unique_ptr<CpuUpload> m_Cpu;
        bool m_BlockActive{false};
        int m_IdleFrames{0};
    };
}
