export module MoleHole:Rendering.Pipeline;

import std;
import vulkan;
import glm;
import GPP;
import :Rendering.Frame;
import :Rendering.Camera;
import :Rendering.Settings;
import :Rendering.Meshes;
import :Simulation.ObjectPaths;

export namespace MoleHole
{
    // What one frame of the viewport is drawn from
    struct RenderInputs
    {
        const GPP::Scene* Scene = nullptr;
        Camera ViewCamera;
        RenderSettings Settings;
        std::shared_ptr<GPP::PhysicsSimulationModule> Physics;
    };

    // Device objects the stages share
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

        // Tracer-resolution targets (see RenderFrameContext::TraceExtent)
        vk::Extent3D TraceExtent{0, 0, 0};
        std::unique_ptr<GPP::VulkanImage> RaytraceImage;
        std::unique_ptr<GPP::VulkanImage> BloomBrightImage;
        std::array<std::unique_ptr<GPP::VulkanImage>, 2> BloomBlurImages;
        std::unique_ptr<GPP::VulkanImage> LensFlareImage;
        // Final blur of the bloom chain; what the composite samples
        GPP::VulkanImage* BloomResult = nullptr;
    };

    // Everything a stage may read while recording
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
        // Viewport buffer and the (scaled) size the tracer renders at
        vk::Extent3D DisplayExtent{0, 0, 0};
        vk::Extent3D TraceExtent{0, 0, 0};
        std::uint32_t GroupsX = 0;
        std::uint32_t GroupsY = 0;
    };

    // One pass or group of passes. A stage the settings turn off records nothing and costs nothing.
    class IRenderStage
    {
    public:
        virtual ~IRenderStage() = default;
        [[nodiscard]] virtual std::string_view Name() const = 0;
        [[nodiscard]] virtual bool Enabled(const RenderSettings&) const { return true; }
        virtual void Record(RenderFrameContext& context) = 0;
        // Shaders compiled; an export waits for every stage
        [[nodiscard]] virtual bool Ready() const { return true; }
    };

    // trace -> bloom -> lens flare -> composite -> overlays -> meshes, each a stage the profile can switch off
    class RenderPipeline
    {
    public:
        RenderPipeline(std::shared_ptr<GPP::Renderer> renderer, std::shared_ptr<GPP::IFileSystem> fileSystem,
                       std::shared_ptr<GPP::EventDispatcher> dispatcher, std::shared_ptr<GPP::Logger> logger,
                       std::shared_ptr<MeshCache> meshes);
        ~RenderPipeline();

        RenderPipeline(const RenderPipeline&) = delete;
        RenderPipeline& operator=(const RenderPipeline&) = delete;

        // Compiles shaders and builds the LUTs; call from the layer's OnAttach
        void Initialize();

        // Trail history of meshes and spheres while the simulation plays
        void RecordObjectPaths(const GPP::Scene& scene);
        void ClearObjectPaths();

        // Render thread: appends this frame's passes to the graph
        void Build(GPP::RenderGraph& graph, const RenderInputs& inputs);

        [[nodiscard]] bool AllStagesReady() const;
        [[nodiscard]] const GPP::GpuCapabilities& Capabilities() const;
        // Names of the stages that recorded in the last Build, in order
        [[nodiscard]] std::vector<std::string> LastRecordedStages() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    // Size the tracer renders at for a viewport and a resolution-scale setting (percent)
    [[nodiscard]] inline vk::Extent3D ScaledTraceExtent(const vk::Extent3D display, const float scalePercent)
    {
        const float scale = std::clamp(scalePercent, 25.0f, 200.0f) / 100.0f;
        const auto scaled = [scale](const std::uint32_t value)
        {
            return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(static_cast<float>(value) * scale)));
        };
        return {scaled(display.width), scaled(display.height), display.depth};
    }
}
