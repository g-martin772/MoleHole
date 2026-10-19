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

    // Disk gas (near circular, inside the Gaussian layer H = h R) is splatted into a 2D grid of column densities in the
    // plane of the disk; its vertical profile is analytic. Everything else stays in the 3D grids.
    struct ParticleDiskGrid
    {
        int Resolution{0};       // cells per axis, 0 = no disk grid (all gas in the 3D grids)
        float Min{0.0f};         // minimum corner of both axes [r_g]
        float Cell{1.0f};        // [r_g]
        float Aspect{0.0f};      // H / R
        float Inner{0.0f};       // [r_g] no disk gas inside
        float Outer{0.0f};       // [r_g] no disk gas outside
        float Schwarzschild{2.0f};
        glm::vec3 Normal{0.0f, 1.0f, 0.0f};
        glm::vec3 E1{1.0f, 0.0f, 0.0f};
        glm::vec3 E2{0.0f, 0.0f, 1.0f};
    };

    inline constexpr int kParticleDiskChannels = 6; // m, m T / T_ref, m v_x, m v_y, m v_z, m s2 with s2 = (z / (h R))^2
    inline constexpr int kParticleDiskSpreadUnits = 16;
    inline constexpr int kParticleDiskMaxLod = 3; // coarsest mip the density estimate may use: 8 cells

    [[nodiscard]] inline int ParticleDiskMipCount(const int resolution)
    {
        if (resolution < 32) return 1;
        return std::clamp(static_cast<int>(std::bit_width(static_cast<unsigned>(resolution))) - 5, 1, 7);
    }

    // The plane grid spans +-1.6 outer radii like the coarse 3D level
    // Gas beyond this many outer radii stays in the 3D grids (streams, tidal tails)
    inline constexpr float kParticleDiskGasReach = 1.25f;

    [[nodiscard]] ParticleDiskGrid MakeParticleDiskGrid(const glm::vec3& normal, double outerRadius, float aspect, float innerRadius,
                                                        float diskOuterRadius, float schwarzschild, int resolution);

    // What the density estimate and the tracers need besides the layout
    struct ParticleBlockExtras
    {
        float MeanMass{0.0f};   // kg, mean particle mass
        float Target3D{0.0f};   // particles per cell asked of the 3D grids (0 = no adaptive smoothing)
        float TargetDisk{0.0f}; // the same for the disk grid
        bool Jitter{false};     // random offset of the march samples inside a step
        ParticleDiskGrid Disk;
    };

    // std140 block shared with shaders/particle_grid.glsl
    struct ParticleGridBlockGpu
    {
        glm::vec4 Min[kParticleGridLevels]{};
        glm::vec4 Info{0.0f, 0.0f, 0.0f, -1.0f};  // voxels per axis, kappa, reference temperature, tracer hole index
        glm::vec4 Frame{0.0f};                    // r_g in metres, fixed-point units per kg (0 = float atomics), kg per unit, mean particle mass
        glm::vec4 Smooth{0.0f};                   // particles per 3D cell asked of the density estimate (0 = off), coarsest mip, step jitter, particles per disk cell
        glm::vec4 PlaneNormal{0.0f, 1.0f, 0.0f, 0.0f}; // disk normal, aspect ratio (0 = no disk grid)
        glm::vec4 PlaneE1{1.0f, 0.0f, 0.0f, 0.0f};     // plane axis, inner radius of disk gas
        glm::vec4 PlaneE2{0.0f, 0.0f, 1.0f, 0.0f};     // plane axis, outer radius of disk gas
        glm::vec4 PlaneGrid{0.0f, 1.0f, 0.0f, 0.0f};   // minimum corner, cell, cells per axis, coarsest mip
        glm::vec4 PlaneInfo{2.0f, 0.0f, 0.0f, 0.0f};   // Schwarzschild radius
    };
    static_assert(sizeof(ParticleGridBlockGpu) == 160);

    inline constexpr int kParticleGridMips = 5; // mip 0 is the resolved grid, 1.. are 2x2x2 box averages (adaptive smoothing)

    [[nodiscard]] inline int ParticleGridMipCount(const int resolution)
    {
        if (resolution < 8) return 1;
        return std::clamp(static_cast<int>(std::bit_width(static_cast<unsigned>(resolution))) - 3, 1, kParticleGridMips);
    }

    struct SplatFixedPoint
    {
        float InverseUnit{1.0f}; // units per kg
        float Unit{1.0f};        // kg per unit
    };

    // 2^-12 of the mean particle mass per unit: one voxel can hold 2^19 mean particles
    [[nodiscard]] SplatFixedPoint MakeSplatFixedPoint(double meanMass);

    [[nodiscard]] ParticleGridBlockGpu MakeParticleGridBlock(const ParticleGridLayout& layout, float kappa,
                                                             float referenceTemperature, int holeIndex, double lengthM,
                                                             const SplatFixedPoint& fixed, bool fixedPoint,
                                                             const ParticleBlockExtras& extras = {});

    // The splat on the CPU: kParticleGridLevels * N^3 * 5 values in the layout of the shader's accumulator, decoded to
    // kg. With `fixed` it quantises like the shader's integer atomics.
    [[nodiscard]] std::vector<float> SplatReference(const GPP::ParticleArrays& particles, const ParticleGridLayout& layout,
                                                    float referenceTemperature, const SplatFixedPoint* fixed,
                                                    const ParticleDiskGrid* disk = nullptr, std::vector<float>* diskWords = nullptr);

    // Share of a particle that is disk gas (the shader's diskWeight) and its squared height in units of H
    [[nodiscard]] float ParticleDiskWeight(const ParticleDiskGrid& disk, const glm::vec3& position, const glm::vec3& velocity, float& s2);

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

        // Allocates the accumulators and textures for N voxels per axis and M plane cells per axis (1 = placeholders); a no-op when unchanged
        void Configure(int resolution, int diskResolution = 1);
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
        [[nodiscard]] int DiskResolution() const noexcept;
        [[nodiscard]] bool UsesFloatAtomics(bool fixedPoint) const noexcept;
        [[nodiscard]] vk::Buffer Block() const;
        [[nodiscard]] vk::Buffer Accumulator() const;
        [[nodiscard]] vk::Buffer DiskAccumulator() const;
        // 3D level 0 A, B, level 1 A, B, then the disk grid A, B
        [[nodiscard]] std::array<std::shared_ptr<GPP::VulkanImage>, 6> Textures() const;
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
