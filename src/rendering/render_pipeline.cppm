export module MoleHole:Rendering.Pipeline;

import std;
import vulkan;
import glm;
import GPP;
import :Rendering.Frame;
import :Rendering.Camera;
import :Rendering.Settings;
import :Rendering.Meshes;
import :Rendering.MeshTrace;
import :Simulation.ObjectPaths;
import :Rendering.OrbitOverlays;

export namespace MoleHole
{
    struct RenderInputs
    {
        const GPP::Scene* Scene = nullptr;
        Camera ViewCamera;
        RenderSettings Settings;
        std::shared_ptr<GPP::PhysicsSimulationModule> Physics;
        std::uint64_t ObserverGuid = 0; // entity whose world line the camera rides (possessed scene camera), 0 = none
    };

    struct MeshTraceFrame
    {
        bool Active = false;
        vk::AccelerationStructureKHR Tlas{};
        vk::Buffer Instances{};
        std::array<const GPP::VulkanImage*, kMeshTextureSlots> Textures{};
        glm::vec4 Bounds{0.0f};
        std::int32_t Count = 0;
    };

    struct RenderResources
    {
        std::shared_ptr<GPP::Renderer> Renderer;
        std::shared_ptr<GPP::IFileSystem> FileSystem;
        std::shared_ptr<GPP::EventDispatcher> Dispatcher;
        std::shared_ptr<GPP::Logger> Logger;
        std::shared_ptr<MeshCache> Meshes;
        std::shared_ptr<GPP::VulkanDevice> Device;
        std::unique_ptr<GPP::VulkanCommandPool> UploadPool;

        std::shared_ptr<GPP::VulkanImage> BlackbodyLut;
        std::shared_ptr<GPP::VulkanImage> AccelerationLut;
        std::shared_ptr<GPP::VulkanImage> HrDiagramLut;
        std::shared_ptr<GPP::VulkanImage> Skybox;
        std::shared_ptr<GPP::VulkanImage> DummyWhiteTexture;

        vk::Extent3D TraceExtent{0, 0, 0};
        std::unique_ptr<GPP::VulkanImage> RaytraceImage;
        std::unique_ptr<GPP::VulkanImage> BloomBrightImage;
        std::array<std::unique_ptr<GPP::VulkanImage>, 2> BloomBlurImages;
        std::unique_ptr<GPP::VulkanImage> LensFlareImage;
        std::unique_ptr<GPP::VulkanImage> ExposureImage; // 1x1, written by the Exposure stage
        GPP::VulkanImage* BloomResult = nullptr;

        MeshTraceFrame MeshTrace;

        // Overlay lines of this frame, from OverlayGeometryStage: rasterised vertices (pos + colour, render units) and
        // the trails the integrated tracer draws lensed
        std::vector<float> OverlayVertices;
        TracedTrails Trails;
        float TrailAngularHalfWidth{0.0f};
        bool TrailsActive{false};
    };

    struct RenderFrameContext
    {
        GPP::RenderGraph& Graph;
        RenderResources& Resources;
        const RenderInputs& Inputs;
        const RenderSettings& Settings;
        const RenderFrame& Frame;
        const Camera& ViewCamera;
        const GPP::Scene& Scene;
        GPP::RenderGraphHandle ColorTarget = GPP::kInvalidRenderGraphHandle;
        GPP::RenderGraphHandle DepthTarget = GPP::kInvalidRenderGraphHandle;
        vk::Format ColorFormat = vk::Format::eUndefined;
        vk::Format DepthFormat = vk::Format::eUndefined;
        vk::Extent3D DisplayExtent{0, 0, 0};
        vk::Extent3D TraceExtent{0, 0, 0};
        std::uint32_t GroupsX = 0;
        std::uint32_t GroupsY = 0;
    };

    class IRenderStage
    {
    public:
        virtual ~IRenderStage() = default;
        [[nodiscard]] virtual std::string_view Name() const = 0;
        [[nodiscard]] virtual bool Enabled(const RenderSettings&) const { return true; }
        virtual void Record(RenderFrameContext& context) = 0;
        [[nodiscard]] virtual bool Ready() const { return true; }
        [[nodiscard]] virtual std::optional<std::string> Failure() const { return std::nullopt; }
    };

    class RenderPipeline
    {
    public:
        RenderPipeline(std::shared_ptr<GPP::Renderer> renderer, std::shared_ptr<GPP::IFileSystem> fileSystem,
                       std::shared_ptr<GPP::EventDispatcher> dispatcher, std::shared_ptr<GPP::Logger> logger,
                       std::shared_ptr<MeshCache> meshes);
        ~RenderPipeline();

        RenderPipeline(const RenderPipeline&) = delete;
        RenderPipeline& operator=(const RenderPipeline&) = delete;

        void Initialize();

        void RecordObjectPaths(const GPP::Scene& scene);
        void ClearObjectPaths();

        void Build(GPP::RenderGraph& graph, const RenderInputs& inputs);

        [[nodiscard]] bool AllStagesReady() const;
        [[nodiscard]] std::optional<std::string> FirstFailure() const;
        [[nodiscard]] const GPP::GpuCapabilities& Capabilities() const;
        [[nodiscard]] std::vector<std::string> LastRecordedStages() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    [[nodiscard]] inline vk::Extent3D ScaledTraceExtent(const vk::Extent3D display, const float scalePercent)
    {
        const float scale = std::clamp(scalePercent, 25.0f, 200.0f) / 100.0f;
        const auto scaled = [scale](const std::uint32_t value)
        {
            return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(static_cast<float>(value) * scale)));
        };
        return {scaled(display.width), scaled(display.height), display.depth};
    }

    [[nodiscard]] inline bool WantsTracedMeshes(const RenderSettings& settings) noexcept
    {
        return settings.RenderMeshes && settings.MeshDrawMode == MeshMode::Traced
               && settings.TransportMode != Transport::Straight;
    }

    enum class TraceKernel { Flat, Marcher, MarcherF64, MarcherMeshes, MarcherMeshesF64 };

    [[nodiscard]] inline TraceKernel SelectTraceKernel(const RenderSettings& settings, const bool f64Available,
                                                       const bool tracedMeshes = false)
    {
        if (settings.TransportMode == Transport::Straight) return TraceKernel::Flat;
        const bool f64 = settings.TransportMode == Transport::Integrated
                         && settings.GpuPrecision == GPP::Units::Precision::F64 && f64Available;
        if (tracedMeshes) return f64 ? TraceKernel::MarcherMeshesF64 : TraceKernel::MarcherMeshes;
        return f64 ? TraceKernel::MarcherF64 : TraceKernel::Marcher;
    }
}
