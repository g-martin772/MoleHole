import GPP;
import MoleHole;
import vulkan;
import glm;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    constexpr int kBlurPasses = 5;
    constexpr float kBloomThreshold = 0.6f;
    constexpr float kBloomIntensity = 5.0f;
    constexpr float kLensFlareIntensity = 0.3f;
    constexpr float kLensFlareThreshold = 2.0f;

    constexpr float kGravityGridPlaneY = -5.0f;
    constexpr float kGravityGridPlaneSize = 200.0f;
    constexpr int kGravityGridResolution = 256;
    constexpr float kGravityGridCellSize = 2.0f;
    constexpr float kGravityGridLineThickness = 0.03f;
    constexpr float kGravityGridOpacity = 0.7f;
    const glm::vec3 kGravityGridColor{0.1f, 0.1f, 0.8f};

    vk::ImageUsageFlags StorageSampledUsage()
    {
        return vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
    }

    ImGuizmo::OPERATION ToImGuizmoOperation(GizmoOperation operation)
    {
        switch (operation)
        {
        case GizmoOperation::Translate: return ImGuizmo::TRANSLATE;
        case GizmoOperation::Rotate: return ImGuizmo::ROTATE;
        case GizmoOperation::Scale: return ImGuizmo::SCALE;
        }
        return ImGuizmo::TRANSLATE;
    }

    struct MeshDrawable
    {
        const GltfPrimitive* Primitive = nullptr;
        glm::mat4 Model{1.0f};
        glm::vec4 BaseColorFactor{0.8f, 0.8f, 0.8f, 1.0f};
        float MetallicFactor{0.5f};
        float RoughnessFactor{0.5f};
        std::shared_ptr<VulkanImage> BaseColorTexture;
    };

    VulkanImageSpecification MakeComputeTargetSpec(vk::Extent3D extent, std::string debugName)
    {
        return VulkanImageSpecification{
            .extent = extent,
            .format = vk::Format::eR32G32B32A32Sfloat,
            .usage = StorageSampledUsage(),
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .createSampler = true,
            .samplerFilter = vk::Filter::eLinear,
            .samplerAddressMode = vk::SamplerAddressMode::eClampToEdge,
            .debugName = std::move(debugName)
        };
    }

    struct ViewportFrameParams
    {
        Camera ViewCamera;
        RenderToggles Render;
    };

    struct PlayState
    {
        std::mutex Mutex;
        std::optional<Scene> Snapshot;
        std::atomic<bool> Active{false};
    };

    struct SimBinding
    {
        std::shared_ptr<SimulationRunner> Runner;
        std::shared_ptr<PlayState> Play = std::make_shared<PlayState>();
    };

    class MeshCache
    {
    public:
        MeshCache(std::shared_ptr<Renderer> renderer, std::shared_ptr<IFileSystem> fileSystem,
                  std::shared_ptr<Logger> logger)
            : m_Renderer(std::move(renderer)), m_FileSystem(std::move(fileSystem)), m_Logger(std::move(logger))
        {
        }

        std::shared_ptr<GltfSceneData> GetOrLoad(const std::string& assetPath)
        {
            if (const auto cached = TryGet(assetPath)) return cached.value_or(nullptr);
            std::scoped_lock loadLock(m_LoadMutex); // one load at a time; a second caller waits and reuses it
            if (const auto cached = TryGet(assetPath)) return cached.value_or(nullptr);

            std::shared_ptr<GltfSceneData> result;
            float radius = 0.5f;
            try
            {
                result = std::make_shared<GltfSceneData>(
                    LoadGltfScene(m_Renderer->GetDevice(), m_FileSystem, assetPath, m_Logger));
                if (!result->Geometry.Vertices.empty())
                {
                    float maxDistSq = 0.0f;
                    for (const auto& vertex : result->Geometry.Vertices)
                    {
                        maxDistSq = std::max(maxDistSq, glm::dot(vertex, vertex));
                    }
                    radius = std::sqrt(maxDistSq);
                }
            }
            catch (const std::exception& error)
            {
                m_Logger->Warn("ViewportLayer: failed to load mesh '{}': {}", assetPath, error.what());
            }
            std::scoped_lock lock(m_Mutex);
            m_Meshes.emplace(assetPath, result);
            m_Radii.emplace(assetPath, radius);
            return result;
        }

        std::optional<std::shared_ptr<GltfSceneData>> TryGet(const std::string& assetPath) const
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Meshes.find(assetPath);
            if (it == m_Meshes.end()) return std::nullopt;
            return it->second;
        }

        float BoundingRadius(const std::string& assetPath) const
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Radii.find(assetPath);
            return it != m_Radii.end() ? it->second : 0.5f;
        }

    private:
        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<Logger> m_Logger;
        mutable std::mutex m_Mutex;
        std::mutex m_LoadMutex;
        std::unordered_map<std::string, std::shared_ptr<GltfSceneData>> m_Meshes;
        std::unordered_map<std::string, float> m_Radii;
    };

    struct SceneLoadResult
    {
        enum class Kind { Startup, File, Template };
        Kind LoadKind = Kind::File;
        bool Ok = false;
        std::string Path;
        std::string SceneName;
        std::string Error;
    };

    struct SceneLoadState
    {
        std::mutex Mutex;
        std::optional<SceneLoadResult> Result;
        std::atomic<bool> InFlight{false};
    };

    void PopulateConvexHulls(GPP::Scene& scene, MeshCache& meshes)
    {
        for (auto [entity, mesh, collider] :
             scene.Registry().view<const MeshComponent, ColliderComponent>().each())
        {
            if (collider.Shape != ColliderShape::ConvexMesh || !collider.ConvexHullPoints.empty()) continue;
            if (mesh.AssetPath.empty()) continue;
            if (const auto gltfScene = meshes.GetOrLoad(mesh.AssetPath); gltfScene && !gltfScene->Geometry.Vertices.empty())
            {
                collider.ConvexHullPoints = gltfScene->Geometry.Vertices;
            }
        }
    }

    void RunSceneLoad(const std::shared_ptr<SceneLoadState>& state, const std::shared_ptr<SceneManager>& scenes,
                      const std::shared_ptr<MeshCache>& meshes, const std::shared_ptr<Logger>& logger,
                      std::string path, SceneLoadResult::Kind kind)
    {
        SceneLoadResult result;
        result.LoadKind = kind;
        result.Path = path;
        try
        {
            auto& scene = scenes->LoadSceneFromFile(path);
            PopulateConvexHulls(scene, *meshes);
            result.Ok = true;
            result.SceneName = scene.Metadata().Name;
        }
        catch (const std::exception& error)
        {
            result.Error = error.what();
            logger->Warn("ViewportLayer: failed to load '{}': {}", path, error.what());
        }
        {
            std::scoped_lock lock(state->Mutex);
            state->Result = std::move(result);
        }
        state->InFlight.store(false, std::memory_order_release);
        state->InFlight.notify_all();
    }

    // raytrace -> bloom extract -> bloom blur (ping-pong) -> lens flare -> composite
    // -> gravity-grid overlay -> physics debug lines -> object-path trails -> mesh overlay
    struct ViewportLayer final : public HotReloadableLayer
    {
        using Dependencies =
            std::tuple<Logger, Renderer, IFileSystem, EventDispatcher, SceneManager, InputState, UiState,
                       AppStateService>;

        ViewportLayer(const std::shared_ptr<Logger>& logger,
                      const std::shared_ptr<Renderer>& renderer,
                      const std::shared_ptr<IFileSystem>& fileSystem,
                      const std::shared_ptr<EventDispatcher>& dispatcher,
                      const std::shared_ptr<SceneManager>& scenes,
                      const std::shared_ptr<InputState>& input,
                      const std::shared_ptr<UiState>& uiState,
                      const std::shared_ptr<AppStateService>& appState)
            : HotReloadableLayer(logger), m_Renderer(renderer), m_FileSystem(fileSystem),
              m_Dispatcher(dispatcher), m_Scenes(scenes), m_Input(input), m_UiState(uiState), m_AppState(appState),
              m_Meshes(std::make_shared<MeshCache>(renderer, fileSystem, logger)),
              m_SceneLoad(std::make_shared<SceneLoadState>())
        {
        }

        void OnAttach() override
        {
            MoleHole::RegisterComponents();
            BeginSceneLoad(StartupScenePath(), SceneLoadResult::Kind::Startup);

            const auto device = m_Renderer->GetDevice();
            m_UploadPool = std::make_unique<VulkanCommandPool>(device, m_Logger, device->GetQueueIndices().Graphics);
            const auto queue = device->GetBackgroundQueue();

            m_RaytracePipeline = MakeComputePipeline(device, "black_hole_rendering.comp");
            m_BloomExtractPipeline = MakeComputePipeline(device, "bloom_extract.comp");
            m_BloomBlurPipeline = MakeComputePipeline(device, "bloom_blur.comp");
            m_LensFlarePipeline = MakeComputePipeline(device, "lens_flare.comp");

            m_ParamsBuffer = std::make_unique<VulkanBuffer>(
                device, MakeUniformBufferSpecification(sizeof(RaytraceParamsGpu)), m_Logger);

            m_BlackbodyLut = GenerateBlackbodyLut(device, *m_UploadPool, queue, m_Logger);
            m_AccelerationLut = GenerateAccelerationLut(device, *m_UploadPool, queue, m_Logger);
            m_HrDiagramLut = GenerateHrDiagramLut(device, *m_UploadPool, queue, m_Logger);
            m_Skybox = LoadSkyboxTexture(device, *m_UploadPool, queue, m_FileSystem,
                                         "assets/backgrounds/space.hdr", m_Logger);
            m_DummyWhiteTexture = GenerateSolidColorTexture(device, *m_UploadPool, queue,
                                                            glm::vec4(1.0f), m_Logger);

            m_MeshCameraBuffer = std::make_unique<VulkanBuffer>(
                device, MakeUniformBufferSpecification(sizeof(MeshCameraParamsGpu)), m_Logger);
            m_GravityGridParamsBuffer = std::make_unique<VulkanBuffer>(
                device, MakeUniformBufferSpecification(sizeof(GravityGridParamsGpu)), m_Logger);
            CreateGravityGridMesh();

            m_StartTime = std::chrono::steady_clock::now();
            m_Logger->Info("ViewportLayer attached");
        }

        void OnDetach() override
        {
            m_SceneLoad->InFlight.wait(true, std::memory_order_acquire);
            if (const auto sim = m_Sim.Load(); sim->Runner) sim->Runner->Stop();
            m_Logger->Info("ViewportLayer detached");
        }

        void OnUpdate(float deltaTime) override
        {
            PollSceneLoad();
            CheckPendingSceneSwitch();
            UpdateCamera(deltaTime);
            ProcessExport();
            if (const auto sim = m_Sim.Load(); sim->Runner)
            {
                if (const auto gravity = sim->Runner->GetModule<GravitySimulationModule>())
                    gravity->SetGravityMultiplier(m_UiState->GravityMultiplier);
                // Tick rate is a UI setting; the runner is told whenever it differs (also covers a
                // freshly started simulation).
                if (sim->Runner->GetTickRate() != m_UiState->SimulationTickRate)
                    sim->Runner->SetTickRate(m_UiState->SimulationTickRate);
            }
            RecordObjectPaths();
            m_FrameParams.Publish(ViewportFrameParams{m_Camera, m_UiState->Render});
        }

        void OnRender() override
        {
            FixupPendingConvexHulls();
            m_ExportPipelinesReady.store(AllExportPipelinesReady(), std::memory_order_release);
        }

        void OnRenderGraph(RenderGraph& graph) override
        {
            const auto sim = m_Sim.Load();
            const auto colorTarget = graph.GetPrimaryColorTarget();
            if (colorTarget == kInvalidRenderGraphHandle || !m_RaytracePipeline || !sim->Runner) return;
            const auto extent = graph.GetImageExtent(colorTarget);
            if (extent.width == 0 || extent.height == 0) return;

            const auto frameParams = m_FrameParams.Load();
            m_RenderCamera = frameParams->ViewCamera;
            m_RenderToggles = frameParams->Render;
            m_RenderCamera.SetAspect(static_cast<float>(extent.width) / static_cast<float>(extent.height));
            m_RenderPhysics = sim->Runner->GetModule<PhysicsSimulationModule>();

            const auto sceneLock = sim->Runner->AcquireSnapshot();
            const Scene& scene = *sceneLock;

            const auto depthTarget = graph.GetPrimaryDepthTarget();
            const auto depthFormat = depthTarget != kInvalidRenderGraphHandle
                                         ? graph.GetImageFormat(depthTarget)
                                         : vk::Format::eUndefined;

            EnsureImages(extent);
            EnsureCompositePipeline(graph.GetImageFormat(colorTarget), depthFormat);
            UploadParams(extent, scene);

            const auto device = m_Renderer->GetDevice();
            const std::uint32_t groupsX = (extent.width + 15u) / 16u;
            const std::uint32_t groupsY = (extent.height + 15u) / 16u;

            graph.AddBudgetedComputePass(
                "ViewportLayer.Raytrace", {}, {}, groupsX * groupsY,
                [this, device, groupsX, set = vk::DescriptorSet{}](const vk::CommandBuffer cmd, RenderGraph& g,
                                                                  const RenderGraphWorkSlice& slice) mutable
                {
                    const auto pipeline = m_RaytracePipeline->GetPipeline();
                    if (!pipeline) return;
                    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->GetPipeline());
                    if (!set)
                    {
                        set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                        DescriptorSetWriter(device->GetDevice())
                            .WriteStorageImage(set, 0, m_RaytraceImage->GetImageView())
                            .WriteUniformBuffer(set, 1, m_ParamsBuffer->GetBuffer())
                            .WriteCombinedImageSampler(set, 2, m_Skybox->GetImageView(), m_Skybox->GetSampler())
                            .WriteCombinedImageSampler(set, 3, m_BlackbodyLut->GetImageView(), m_BlackbodyLut->GetSampler())
                            .WriteCombinedImageSampler(set, 4, m_AccelerationLut->GetImageView(), m_AccelerationLut->GetSampler())
                            .WriteCombinedImageSampler(set, 5, m_HrDiagramLut->GetImageView(), m_HrDiagramLut->GetSampler())
                            .Update();
                    }
                    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->GetLayout(), 0, set, {});
                    const std::array<std::uint32_t, 2> push{slice.First, groupsX};
                    cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0,
                                      sizeof(push), push.data());
                    cmd.dispatch(slice.Count, 1, 1);
                });

            graph.AddComputePass(
                "ViewportLayer.BloomExtract", {}, {},
                [this, device, groupsX, groupsY](const vk::CommandBuffer cmd, RenderGraph& g)
                {
                    const auto pipeline = m_BloomExtractPipeline->GetPipeline();
                    if (!pipeline) return;
                    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->GetPipeline());
                    const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                    DescriptorSetWriter(device->GetDevice())
                        .WriteStorageImage(set, 0, m_RaytraceImage->GetImageView())
                        .WriteStorageImage(set, 1, m_BloomBrightImage->GetImageView())
                        .Update();
                    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->GetLayout(), 0, set, {});
                    const float threshold = kBloomThreshold;
                    cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(float), &threshold);
                    cmd.dispatch(groupsX, groupsY, 1);
                });

            int finalBlurIndex = 0;
            VulkanImage* src = m_BloomBrightImage.get();
            for (int i = 0; i < kBlurPasses * 2; ++i)
            {
                const int horizontal = (i % 2 == 0) ? 1 : 0;
                const int dstIndex = horizontal ? 0 : 1;
                VulkanImage* dst = m_BloomBlurImages[dstIndex].get();
                graph.AddComputePass(
                    "ViewportLayer.BloomBlur", {}, {},
                    [this, device, groupsX, groupsY, src, dst, horizontal](const vk::CommandBuffer cmd, RenderGraph& g)
                    {
                        const auto pipeline = m_BloomBlurPipeline->GetPipeline();
                        if (!pipeline) return;
                        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->GetPipeline());
                        const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                        DescriptorSetWriter(device->GetDevice())
                            .WriteStorageImage(set, 0, src->GetImageView())
                            .WriteStorageImage(set, 1, dst->GetImageView())
                            .Update();
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->GetLayout(), 0, set, {});
                        cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(int), &horizontal);
                        cmd.dispatch(groupsX, groupsY, 1);
                    });
                src = dst;
                finalBlurIndex = dstIndex;
            }
            VulkanImage* finalBlurImage = m_BloomBlurImages[finalBlurIndex].get();

            graph.AddComputePass(
                "ViewportLayer.LensFlare", {}, {},
                [this, device, groupsX, groupsY, finalBlurImage](const vk::CommandBuffer cmd, RenderGraph& g)
                {
                    const auto pipeline = m_LensFlarePipeline->GetPipeline();
                    if (!pipeline) return;
                    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->GetPipeline());
                    const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                    DescriptorSetWriter(device->GetDevice())
                        .WriteStorageImage(set, 0, finalBlurImage->GetImageView())
                        .WriteStorageImage(set, 1, m_LensFlareImage->GetImageView())
                        .Update();
                    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->GetLayout(), 0, set, {});
                    struct { float Intensity; float Threshold; std::int32_t Enabled; } push{
                        kLensFlareIntensity, kLensFlareThreshold, 1
                    };
                    cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push);
                    cmd.dispatch(groupsX, groupsY, 1);
                });

            if (m_CompositePipeline)
            {
                std::optional<RenderGraphAttachment> compositeDepthAttachment;
                if (depthTarget != kInvalidRenderGraphHandle)
                {
                    compositeDepthAttachment = RenderGraphAttachment{
                        .Handle = depthTarget, .LoadOp = vk::AttachmentLoadOp::eClear,
                        .Clear = vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0))
                    };
                }
                graph.AddGraphicsPass(
                    "ViewportLayer.Composite", {}, {},
                    {RenderGraphAttachment{
                        .Handle = colorTarget, .LoadOp = vk::AttachmentLoadOp::eClear,
                        .Clear = vk::ClearValue(vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f))
                    }},
                    compositeDepthAttachment,
                    [this, device, extent, finalBlurImage](const vk::CommandBuffer cmd, RenderGraph& g)
                    {
                        const auto pipeline = m_CompositePipeline->GetPipeline();
                        if (!pipeline) return;
                        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->GetPipeline());
                        const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                        DescriptorSetWriter(device->GetDevice())
                            .WriteCombinedImageSampler(set, 0, m_RaytraceImage->GetImageView(), m_RaytraceImage->GetSampler(),
                                                       vk::ImageLayout::eGeneral)
                            .WriteCombinedImageSampler(set, 1, finalBlurImage->GetImageView(), finalBlurImage->GetSampler(),
                                                       vk::ImageLayout::eGeneral)
                            .WriteCombinedImageSampler(set, 2, m_LensFlareImage->GetImageView(), m_LensFlareImage->GetSampler(),
                                                       vk::ImageLayout::eGeneral)
                            .Update();
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline->GetLayout(), 0, set, {});
                        struct
                        {
                            std::int32_t FxaaEnabled;
                            std::int32_t BloomEnabled;
                            float BloomIntensity;
                            std::int32_t BloomDebug;
                            std::int32_t LensFlareEnabled;
                            float LensFlareIntensity;
                            float RtWidth;
                            float RtHeight;
                        } push{1, 1, kBloomIntensity, 0, 1, kLensFlareIntensity,
                               static_cast<float>(extent.width), static_cast<float>(extent.height)};
                        cmd.pushConstants(pipeline->GetLayout(),
                                          vk::ShaderStageFlagBits::eFragment, 0, sizeof(push), &push);
                        cmd.draw(3, 1, 0, 0);
                    });
            }

            if (m_RenderToggles.ShowGravityGrid)
            {
                EnsureGravityGridPipeline(graph.GetImageFormat(colorTarget));
                if (m_GravityGridPipeline)
                {
                    UploadGravityGridParams(scene);
                    graph.AddGraphicsPass(
                        "ViewportLayer.GravityGrid", {}, {},
                        {RenderGraphAttachment{.Handle = colorTarget, .LoadOp = vk::AttachmentLoadOp::eLoad}},
                        std::nullopt,
                        [this, device](const vk::CommandBuffer cmd, RenderGraph& g)
                        {
                            const auto pipeline = m_GravityGridPipeline->GetPipeline();
                            if (!pipeline || m_GravityGridIndexCount == 0) return;
                            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->GetPipeline());
                            const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                            DescriptorSetWriter(device->GetDevice())
                                .WriteUniformBuffer(set, 0, m_GravityGridParamsBuffer->GetBuffer())
                                .Update();
                            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline->GetLayout(), 0, set, {});
                            BindVertexBuffer(cmd, m_GravityGridVertexBuffer->GetBuffer());
                            BindIndexBuffer(cmd, m_GravityGridIndexBuffer->GetBuffer());
                            cmd.drawIndexed(m_GravityGridIndexCount, 1, 0, 0, 0);
                        });
                }
            }

            UpdatePhysicsDebugLines();
            if (m_PhysicsDebugLineVertexCount > 0)
            {
                EnsurePhysicsDebugPipeline(graph.GetImageFormat(colorTarget));
                if (m_PhysicsDebugPipeline)
                {
                    const glm::mat4 viewProjection = m_RenderCamera.GetViewProjectionMatrix();
                    graph.AddGraphicsPass(
                        "ViewportLayer.PhysicsDebug", {}, {},
                        {RenderGraphAttachment{.Handle = colorTarget, .LoadOp = vk::AttachmentLoadOp::eLoad}},
                        std::nullopt,
                        [this, viewProjection](const vk::CommandBuffer cmd, RenderGraph& g)
                        {
                            const auto pipeline = m_PhysicsDebugPipeline->GetPipeline();
                            if (!pipeline) return;
                            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->GetPipeline());
                            cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eVertex, 0,
                                              sizeof(glm::mat4), &viewProjection);
                            BindVertexBuffer(cmd, m_PhysicsDebugVertexBuffer->GetBuffer());
                            cmd.draw(m_PhysicsDebugLineVertexCount, 1, 0, 0);
                        });
                }
            }

            UpdateObjectPathLines();
            if (m_ObjectPathLineVertexCount > 0)
            {
                EnsurePhysicsDebugPipeline(graph.GetImageFormat(colorTarget));
                if (m_PhysicsDebugPipeline)
                {
                    const glm::mat4 viewProjection = m_RenderCamera.GetViewProjectionMatrix();
                    graph.AddGraphicsPass(
                        "ViewportLayer.ObjectPaths", {}, {},
                        {RenderGraphAttachment{.Handle = colorTarget, .LoadOp = vk::AttachmentLoadOp::eLoad}},
                        std::nullopt,
                        [this, viewProjection](const vk::CommandBuffer cmd, RenderGraph& g)
                        {
                            const auto pipeline = m_PhysicsDebugPipeline->GetPipeline();
                            if (!pipeline) return;
                            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->GetPipeline());
                            cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eVertex, 0,
                                              sizeof(glm::mat4), &viewProjection);
                            BindVertexBuffer(cmd, m_ObjectPathVertexBuffer->GetBuffer());
                            cmd.draw(m_ObjectPathLineVertexCount, 1, 0, 0);
                        });
                }
            }

            if (depthTarget != kInvalidRenderGraphHandle)
            {
                EnsureMeshPipeline(graph.GetImageFormat(colorTarget), depthFormat);
                auto drawables = CollectMeshDrawables(scene);
                if (m_MeshPipeline && !drawables.empty())
                {
                    UploadMeshCamera();
                    graph.AddGraphicsPass(
                        "ViewportLayer.Mesh", {}, {},
                        {RenderGraphAttachment{.Handle = colorTarget, .LoadOp = vk::AttachmentLoadOp::eLoad}},
                        RenderGraphAttachment{.Handle = depthTarget, .LoadOp = vk::AttachmentLoadOp::eLoad},
                        [this, device, drawables = std::move(drawables)](const vk::CommandBuffer cmd, RenderGraph& g)
                        {
                            const auto pipeline = m_MeshPipeline->GetPipeline();
                            if (!pipeline) return;
                            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->GetPipeline());
                            for (const auto& drawable : drawables)
                            {
                                const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                                const auto* texture = drawable.BaseColorTexture
                                                           ? drawable.BaseColorTexture.get()
                                                           : m_DummyWhiteTexture.get();
                                DescriptorSetWriter(device->GetDevice())
                                    .WriteUniformBuffer(set, 0, m_MeshCameraBuffer->GetBuffer())
                                    .WriteCombinedImageSampler(set, 1, texture->GetImageView(), texture->GetSampler())
                                    .Update();
                                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline->GetLayout(), 0, set, {});

                                MeshPushConstantsGpu push{};
                                push.Model = drawable.Model;
                                push.BaseColorFactor = drawable.BaseColorFactor;
                                push.MetallicFactor = drawable.MetallicFactor;
                                push.RoughnessFactor = drawable.RoughnessFactor;
                                push.HasBaseColorTexture = drawable.BaseColorTexture ? 1 : 0;
                                cmd.pushConstants(pipeline->GetLayout(),
                                                  vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                                  0, sizeof(push), &push);

                                BindVertexBuffer(cmd, drawable.Primitive->VertexBuffer.GetBuffer());
                                BindIndexBuffer(cmd, drawable.Primitive->IndexBuffer.GetBuffer());
                                cmd.drawIndexed(drawable.Primitive->IndexCount, 1, 0, 0, 0);
                            }
                        });
                }
            }
        }

        void OnUiRender() override
        {
            ImGuizmo::BeginFrame();
            const bool viewportOpen = ImGui::Begin("Viewport");
            m_UiState->ViewportVisible = viewportOpen;
            m_Renderer->SetBufferTargetVisible(m_LayerTarget.Id, viewportOpen || m_UiState->ExportActive);
            if (!viewportOpen)
            {
                m_UiState->ViewportScreenMin = {0.0f, 0.0f};
                m_UiState->ViewportScreenMax = {0.0f, 0.0f};
                ImGui::End();
                return;
            }

            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (!m_UiState->ExportActive && avail.x >= 1.0f && avail.y >= 1.0f)
            {
                m_Camera.SetAspect(avail.x / avail.y);
                m_Renderer->ResizeBufferTarget(
                    m_LayerTarget.Id,
                    glm::uvec2{static_cast<std::uint32_t>(avail.x), static_cast<std::uint32_t>(avail.y)});
            }

            if (const auto target = m_Renderer->GetRenderTargetInfo(m_LayerTarget.Id))
            {
                ImGui::Image(reinterpret_cast<ImTextureID>(target->ImGuiTexture), avail);
                const bool imageHovered = ImGui::IsItemHovered();
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                m_UiState->ViewportScreenMin = {min.x, min.y};
                m_UiState->ViewportScreenMax = {max.x, max.y};

                if (!m_UiState->ExportActive)
                {
                    const bool toolbarHovered = RenderGizmoToolbar(min, max);
                    UpdateGizmoAndPicking(min, max, imageHovered, toolbarHovered);
                }
            }
            else
            {
                ImGui::TextUnformatted("Viewport buffer not ready yet.");
            }

            if (m_UiState->ExportActive)
            {
                ImGui::SetCursorPos(ImVec2(8, ImGui::GetFrameHeight() + 8));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.6f));
                ImGui::BeginChild("##ExportProgress", ImVec2(260, 48), true, ImGuiWindowFlags_NoScrollbar);
                ImGui::TextUnformatted(m_UiState->ExportStatus.c_str());
                ImGui::ProgressBar(m_UiState->ExportProgress, ImVec2(-1, 0));
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            ImGui::End();
        }

    private:
        [[nodiscard]] bool RenderGizmoToolbar(ImVec2 viewportMin, ImVec2 viewportMax)
        {
            struct ButtonSpec { const char* Label; GizmoOperation Op; const char* Tooltip; };
            const std::array<ButtonSpec, 3> operationButtons{{
                {"Move", GizmoOperation::Translate, "Translate (T / 1)"},
                {"Rotate", GizmoOperation::Rotate, "Rotate (R / 2)"},
                {"Scale", GizmoOperation::Scale, "Scale (S / 3)"},
            }};
            const char* modeLabel = m_UiState->ActiveGizmoMode == GizmoMode::Local ? "Local" : "World";

            const ImVec2 framePad = ImGui::GetStyle().FramePadding;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float rowHeight = ImGui::GetFrameHeight();
            const auto buttonSize = [&](const char* label)
            {
                return ImVec2(ImGui::CalcTextSize(label).x + framePad.x * 2.0f, rowHeight);
            };

            std::array<ImVec2, operationButtons.size()> opSizes{};
            float contentWidth = 0.0f;
            for (std::size_t i = 0; i < operationButtons.size(); ++i)
            {
                opSizes[i] = buttonSize(operationButtons[i].Label);
                contentWidth += opSizes[i].x + spacing;
            }
            const ImVec2 modeSize = buttonSize(modeLabel);
            contentWidth += modeSize.x + spacing;
            contentWidth += rowHeight + spacing + ImGui::CalcTextSize("Snap").x;

            constexpr float kOuterPadding = 8.0f;
            constexpr float kInnerPadding = 8.0f;
            const ImVec2 panelSize(contentWidth + kInnerPadding * 2.0f, rowHeight + kInnerPadding * 2.0f);
            const ImVec2 panelMin(viewportMax.x - panelSize.x - kOuterPadding, viewportMin.y + kOuterPadding);

            ImGui::GetWindowDrawList()->AddRectFilled(
                panelMin, ImVec2(panelMin.x + panelSize.x, panelMin.y + panelSize.y),
                ImGui::GetColorU32(ImGuiCol_ChildBg), 4.0f);

            ImGui::SetCursorScreenPos(ImVec2(panelMin.x + kInnerPadding, panelMin.y + kInnerPadding));
            ImGui::BeginGroup();
            for (std::size_t i = 0; i < operationButtons.size(); ++i)
            {
                const auto& button = operationButtons[i];
                const bool active = m_UiState->ActiveGizmoOperation == button.Op;
                if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                if (ImGui::Button(button.Label, opSizes[i])) m_UiState->ActiveGizmoOperation = button.Op;
                if (active) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", button.Tooltip);
                ImGui::SameLine();
            }

            if (ImGui::Button(modeLabel, modeSize))
            {
                m_UiState->ActiveGizmoMode =
                    m_UiState->ActiveGizmoMode == GizmoMode::Local ? GizmoMode::World : GizmoMode::Local;
            }
            ImGui::SameLine();
            ImGui::Checkbox("Snap", &m_UiState->GizmoSnapEnabled);
            ImGui::EndGroup();

            return ImGui::IsMouseHoveringRect(panelMin, ImVec2(panelMin.x + panelSize.x, panelMin.y + panelSize.y));
        }

        void UpdateGizmoAndPicking(ImVec2 min, ImVec2 max, bool imageHovered, bool toolbarHovered)
        {
            if (imageHovered && !ImGui::GetIO().WantTextInput)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_1) || ImGui::IsKeyPressed(ImGuiKey_T))
                    m_UiState->ActiveGizmoOperation = GizmoOperation::Translate;
                if (ImGui::IsKeyPressed(ImGuiKey_2) || ImGui::IsKeyPressed(ImGuiKey_R))
                    m_UiState->ActiveGizmoOperation = GizmoOperation::Rotate;
                if (ImGui::IsKeyPressed(ImGuiKey_3) || ImGui::IsKeyPressed(ImGuiKey_S))
                    m_UiState->ActiveGizmoOperation = GizmoOperation::Scale;
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_UiState->SelectedEntityGuid = 0;
            }

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(min.x, min.y, max.x - min.x, max.y - min.y);

            bool gizmoActive = false;
            const auto sim = m_Sim.Load();
            if (m_UiState->SelectedEntityGuid != 0 && sim->Runner)
            {
                gizmoActive = ManipulateSelectedEntity(*sim);
            }

            if (imageHovered && !toolbarHovered && !gizmoActive && !ImGuizmo::IsOver() && sim->Runner &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                PickEntityUnderMouse(min, max, *sim);
            }
        }

        bool ManipulateSelectedEntity(const SimBinding& sim)
        {
            glm::mat4 model(1.0f);
            bool hasTransform = false;
            {
                const auto sceneLock = sim.Runner->AcquireSnapshot();
                const Scene& scene = *sceneLock;
                const auto entity = scene.FindByGuid(m_UiState->SelectedEntityGuid);
                if (scene.IsValid(entity) && scene.Registry().all_of<TransformComponent>(entity))
                {
                    model = scene.Registry().get<TransformComponent>(entity).GetMatrix();
                    hasTransform = true;
                }
            }
            if (!hasTransform)
            {
                m_UiState->SelectedEntityGuid = 0;
                return false;
            }

            const glm::mat4 view = m_Camera.GetViewMatrix();
            glm::mat4 projection = m_Camera.GetProjectionMatrix();
            projection[1][1] *= -1.0f;
            const auto operation = ToImGuizmoOperation(m_UiState->ActiveGizmoOperation);
            const auto mode = m_UiState->ActiveGizmoMode == GizmoMode::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

            float snapValues[3]{};
            const float* snapPtr = nullptr;
            if (m_UiState->GizmoSnapEnabled)
            {
                switch (m_UiState->ActiveGizmoOperation)
                {
                case GizmoOperation::Translate:
                    snapValues[0] = m_UiState->GizmoTranslateSnap.x;
                    snapValues[1] = m_UiState->GizmoTranslateSnap.y;
                    snapValues[2] = m_UiState->GizmoTranslateSnap.z;
                    break;
                case GizmoOperation::Rotate:
                    snapValues[0] = snapValues[1] = snapValues[2] = m_UiState->GizmoRotateSnapDegrees;
                    break;
                case GizmoOperation::Scale:
                    snapValues[0] = snapValues[1] = snapValues[2] = m_UiState->GizmoScaleSnap;
                    break;
                }
                snapPtr = snapValues;
            }

            const bool manipulated = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                                                           operation, mode, glm::value_ptr(model), nullptr, snapPtr);
            if (manipulated)
            {
                float t[3], r[3], s[3];
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), t, r, s);
                const glm::vec3 position(t[0], t[1], t[2]);
                const glm::quat rotation(glm::radians(glm::vec3(r[0], r[1], r[2])));
                const glm::vec3 scale(s[0], s[1], s[2]);
                const auto guid = m_UiState->SelectedEntityGuid;
                sim.Runner->EnqueueTrackedEdit([guid, position, rotation, scale](Scene& scene)
                {
                    const auto entity = scene.FindByGuid(guid);
                    if (scene.IsValid(entity) && scene.Registry().all_of<TransformComponent>(entity))
                    {
                        auto& transform = scene.Registry().get<TransformComponent>(entity);
                        transform.Position = position;
                        transform.Rotation = rotation;
                        transform.Scale = scale;
                        scene.MarkDirty(entity);
                    }
                });
            }

            return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        }

        void PickEntityUnderMouse(ImVec2 min, ImVec2 max, const SimBinding& sim)
        {
            const glm::vec2 viewportMin{min.x, min.y};
            const glm::vec2 viewportSize{max.x - min.x, max.y - min.y};
            if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return;

            const auto mousePos = ImGui::GetMousePos();
            const auto ray = ScreenPointToRay({mousePos.x, mousePos.y}, viewportMin, viewportSize,
                                              m_Camera.GetViewMatrix(), m_Camera.GetProjectionMatrix(),
                                              m_Camera.GetPosition());

            const auto sceneLock = sim.Runner->AcquireSnapshot();
            const Scene& scene = *sceneLock;
            const auto radiusOverride = [this](const Scene& s, entt::entity entity) -> std::optional<float>
            {
                const auto* mesh = s.Registry().try_get<const MeshComponent>(entity);
                if (!mesh || mesh->AssetPath.empty()) return std::nullopt;
                const float localRadius = m_Meshes->BoundingRadius(mesh->AssetPath);
                if (const auto* transform = s.Registry().try_get<const TransformComponent>(entity))
                {
                    return localRadius * std::max({transform->Scale.x, transform->Scale.y, transform->Scale.z});
                }
                return localRadius;
            };
            if (const auto hit = PickClosestEntity(scene, ray.Origin, ray.Direction, radiusOverride))
            {
                m_UiState->SelectedEntityGuid = scene.Registry().get<MetadataComponent>(hit->Entity).Guid;
            }
            else
            {
                m_UiState->SelectedEntityGuid = 0;
            }
        }

        std::shared_ptr<ShaderPipeline> MakeComputePipeline(const std::shared_ptr<VulkanDevice>& device,
                                                             std::string fileName)
        {
            auto pipeline = std::make_shared<ShaderPipeline>(
                device,
                VulkanPipelineSpecification{},
                ShaderPipelineDescription{
                    .compute = ShaderSource{
                        .path = m_FileSystem->ResolveAssetPath("shaders", fileName),
                        .stage = ShaderStage::Compute
                    },
                    .enableHotReload = true
                },
                m_FileSystem, m_Dispatcher, m_Logger);
            if (!pipeline->StartOnRenderThread())
            {
                m_Logger->Error("ViewportLayer: failed to compile compute shader '{}'", fileName);
            }
            return pipeline;
        }

        void EnsureCompositePipeline(vk::Format colorFormat, vk::Format depthFormat)
        {
            if (m_CompositePipeline && m_CompositeColorFormat == colorFormat
                && m_CompositeDepthFormat == depthFormat) return;
            const auto device = m_Renderer->GetDevice();
            m_CompositePipeline = std::make_shared<ShaderPipeline>(
                device,
                VulkanPipelineSpecification{
                    .colorFormat = colorFormat,
                    .depthFormat = depthFormat,
                    .enableBlending = false,
                    .cullMode = vk::CullModeFlagBits::eNone
                },
                ShaderPipelineDescription{
                    .vertex = ShaderSource{
                        .path = m_FileSystem->ResolveAssetPath("shaders", "blackhole_display.vert"),
                        .stage = ShaderStage::Vertex
                    },
                    .fragment = ShaderSource{
                        .path = m_FileSystem->ResolveAssetPath("shaders", "blackhole_display.frag"),
                        .stage = ShaderStage::Fragment
                    },
                    .enableHotReload = true
                },
                m_FileSystem, m_Dispatcher, m_Logger);
            if (!m_CompositePipeline->StartOnRenderThread())
            {
                m_Logger->Error("ViewportLayer: failed to compile composite display shader");
            }
            m_CompositeColorFormat = colorFormat;
            m_CompositeDepthFormat = depthFormat;
        }

        void EnsureMeshPipeline(vk::Format colorFormat, vk::Format depthFormat)
        {
            if (m_MeshPipeline && m_MeshColorFormat == colorFormat && m_MeshDepthFormat == depthFormat) return;
            const auto device = m_Renderer->GetDevice();
            m_MeshPipeline = std::make_shared<ShaderPipeline>(
                device,
                VulkanPipelineSpecification{
                    .colorFormat = colorFormat,
                    .depthFormat = depthFormat,
                    .enableBlending = false,
                    .cullMode = vk::CullModeFlagBits::eNone
                },
                ShaderPipelineDescription{
                    .vertex = ShaderSource{
                        .path = m_FileSystem->ResolveAssetPath("shaders", "mesh.vert"),
                        .stage = ShaderStage::Vertex
                    },
                    .fragment = ShaderSource{
                        .path = m_FileSystem->ResolveAssetPath("shaders", "mesh.frag"),
                        .stage = ShaderStage::Fragment
                    },
                    .enableHotReload = true
                },
                m_FileSystem, m_Dispatcher, m_Logger);
            if (!m_MeshPipeline->StartOnRenderThread())
            {
                m_Logger->Error("ViewportLayer: failed to compile mesh overlay shader");
            }
            m_MeshColorFormat = colorFormat;
            m_MeshDepthFormat = depthFormat;
        }

        void EnsureGravityGridPipeline(vk::Format colorFormat)
        {
            if (m_GravityGridPipeline && m_GravityGridColorFormat == colorFormat) return;
            const auto device = m_Renderer->GetDevice();
            m_GravityGridPipeline = std::make_shared<ShaderPipeline>(
                device,
                VulkanPipelineSpecification{
                    .colorFormat = colorFormat,
                    .depthFormat = vk::Format::eUndefined,
                    .enableBlending = true,
                    .cullMode = vk::CullModeFlagBits::eNone
                },
                ShaderPipelineDescription{
                    .vertex = ShaderSource{
                        .path = m_FileSystem->ResolveAssetPath("shaders", "plane_grid.vert"),
                        .stage = ShaderStage::Vertex
                    },
                    .fragment = ShaderSource{
                        .path = m_FileSystem->ResolveAssetPath("shaders", "plane_grid.frag"),
                        .stage = ShaderStage::Fragment
                    },
                    .enableHotReload = true
                },
                m_FileSystem, m_Dispatcher, m_Logger);
            if (!m_GravityGridPipeline->StartOnRenderThread())
            {
                m_Logger->Error("ViewportLayer: failed to compile gravity grid shader");
            }
            m_GravityGridColorFormat = colorFormat;
        }

        void EnsurePhysicsDebugPipeline(vk::Format colorFormat)
        {
            if (m_PhysicsDebugPipeline && m_PhysicsDebugColorFormat == colorFormat) return;
            const auto device = m_Renderer->GetDevice();
            m_PhysicsDebugPipeline = std::make_shared<ShaderPipeline>(
                device,
                VulkanPipelineSpecification{
                    .colorFormat = colorFormat,
                    .depthFormat = vk::Format::eUndefined,
                    .enableBlending = false,
                    .cullMode = vk::CullModeFlagBits::eNone,
                    .topology = vk::PrimitiveTopology::eLineList
                },
                ShaderPipelineDescription{
                    .vertex = ShaderSource{
                        .path = m_FileSystem->ResolveAssetPath("shaders", "physics_debug_line.vert"),
                        .stage = ShaderStage::Vertex
                    },
                    .fragment = ShaderSource{
                        .path = m_FileSystem->ResolveAssetPath("shaders", "physics_debug_line.frag"),
                        .stage = ShaderStage::Fragment
                    },
                    .enableHotReload = true
                },
                m_FileSystem, m_Dispatcher, m_Logger);
            if (!m_PhysicsDebugPipeline->StartOnRenderThread())
            {
                m_Logger->Error("ViewportLayer: failed to compile physics debug line shader");
            }
            m_PhysicsDebugColorFormat = colorFormat;
        }

        static constexpr std::size_t kMaxRetiredDebugBuffers = 3;
        std::deque<std::shared_ptr<VulkanBuffer>> m_RetiredDebugBuffers;

        void RetireDebugBuffer(std::unique_ptr<VulkanBuffer> oldBuffer)
        {
            if (!oldBuffer) return;
            m_RetiredDebugBuffers.push_back(std::shared_ptr<VulkanBuffer>(std::move(oldBuffer)));
            while (m_RetiredDebugBuffers.size() > kMaxRetiredDebugBuffers)
            {
                m_RetiredDebugBuffers.pop_front();
            }
        }

        void AppendPhysicsDebugVertex(std::vector<float>& out, const physx::PxVec3& position, physx::PxU32 color)
        {
            out.push_back(position.x);
            out.push_back(position.y);
            out.push_back(position.z);
            out.push_back(static_cast<float>((color >> 16) & 0xFF) / 255.0f);
            out.push_back(static_cast<float>((color >> 8) & 0xFF) / 255.0f);
            out.push_back(static_cast<float>(color & 0xFF) / 255.0f);
        }

        void UpdatePhysicsDebugLines()
        {
            m_PhysicsDebugLineVertexCount = 0;
            if (!m_RenderPhysics) return;

            const bool enabled = m_RenderToggles.ShowPhysicsDebug;
            m_RenderPhysics->SetDebugVisualizationEnabled(enabled);
            if (!enabled) return;

            const auto lines = m_RenderPhysics->GetDebugLines();
            if (lines.empty()) return;

            std::vector<float> vertices;
            vertices.reserve(lines.size() * 2 * 6);
            for (const auto& line : lines)
            {
                AppendPhysicsDebugVertex(vertices, line.pos0, line.color0);
                AppendPhysicsDebugVertex(vertices, line.pos1, line.color1);
            }

            const auto device = m_Renderer->GetDevice();
            const auto byteSize = static_cast<vk::DeviceSize>(vertices.size() * sizeof(float));
            if (!m_PhysicsDebugVertexBuffer || m_PhysicsDebugVertexBuffer->GetSize() < byteSize)
            {
                RetireDebugBuffer(std::move(m_PhysicsDebugVertexBuffer));
                m_PhysicsDebugVertexBuffer = std::make_unique<VulkanBuffer>(
                    device, MakeVertexBufferSpecification(byteSize, true), m_Logger);
            }
            m_PhysicsDebugVertexBuffer->Upload(vertices.data(), byteSize);
            m_PhysicsDebugLineVertexCount = static_cast<std::uint32_t>(lines.size()) * 2;
        }

        void RecordObjectPaths()
        {
            const auto sim = m_Sim.Load();
            if (!sim->Runner) return;
            if (sim->Runner->IsPaused()) return;
            const auto sceneLock = sim->Runner->AcquireSnapshot();
            std::scoped_lock lock(m_PathMutex);
            m_ObjectPathTracker.RecordPositions(*sceneLock);
        }

        void AppendObjectPathSegment(std::vector<float>& out, const glm::vec3& a, const glm::vec3& b,
                                     const glm::vec3& color)
        {
            out.insert(out.end(), {a.x, a.y, a.z, color.r, color.g, color.b});
            out.insert(out.end(), {b.x, b.y, b.z, color.r, color.g, color.b});
        }

        void UpdateObjectPathLines()
        {
            m_ObjectPathLineVertexCount = 0;
            if (!m_RenderToggles.ShowObjectPaths) return;

            static const glm::vec3 kMeshColor{0.2f, 0.8f, 0.2f};
            static const glm::vec3 kSphereColor{0.8f, 0.2f, 0.8f};

            std::scoped_lock pathLock(m_PathMutex);
            std::vector<float> vertices;
            for (const auto& [guid, history] : m_ObjectPathTracker.MeshHistories())
            {
                for (std::size_t i = 1; i < history.size(); ++i)
                {
                    AppendObjectPathSegment(vertices, history[i - 1], history[i], kMeshColor);
                }
            }
            for (const auto& [guid, history] : m_ObjectPathTracker.SphereHistories())
            {
                for (std::size_t i = 1; i < history.size(); ++i)
                {
                    AppendObjectPathSegment(vertices, history[i - 1], history[i], kSphereColor);
                }
            }
            if (vertices.empty()) return;

            const auto device = m_Renderer->GetDevice();
            const auto byteSize = static_cast<vk::DeviceSize>(vertices.size() * sizeof(float));
            if (!m_ObjectPathVertexBuffer || m_ObjectPathVertexBuffer->GetSize() < byteSize)
            {
                RetireDebugBuffer(std::move(m_ObjectPathVertexBuffer));
                m_ObjectPathVertexBuffer = std::make_unique<VulkanBuffer>(
                    device, MakeVertexBufferSpecification(byteSize, true), m_Logger);
            }
            m_ObjectPathVertexBuffer->Upload(vertices.data(), byteSize);
            // 6 floats (pos+color) per vertex, 2 vertices per segment.
            m_ObjectPathLineVertexCount = static_cast<std::uint32_t>(vertices.size() / 6);
        }

        void CreateGravityGridMesh()
        {
            constexpr int N = kGravityGridResolution;
            constexpr int vertsPerSide = N + 1;
            std::vector<glm::vec3> vertices;
            vertices.reserve(static_cast<std::size_t>(vertsPerSide) * vertsPerSide);
            const float half = kGravityGridPlaneSize * 0.5f;
            for (int z = 0; z <= N; ++z)
            {
                const float wz = -half + (static_cast<float>(z) / N) * kGravityGridPlaneSize;
                for (int x = 0; x <= N; ++x)
                {
                    const float wx = -half + (static_cast<float>(x) / N) * kGravityGridPlaneSize;
                    vertices.emplace_back(wx, kGravityGridPlaneY, wz);
                }
            }

            std::vector<std::uint32_t> indices;
            indices.reserve(static_cast<std::size_t>(N) * N * 6);
            for (int z = 0; z < N; ++z)
            {
                for (int x = 0; x < N; ++x)
                {
                    const auto i0 = static_cast<std::uint32_t>(z * vertsPerSide + x);
                    const std::uint32_t i1 = i0 + 1;
                    const std::uint32_t i2 = i0 + vertsPerSide;
                    const std::uint32_t i3 = i2 + 1;
                    indices.insert(indices.end(), {i0, i2, i1, i1, i2, i3});
                }
            }

            const auto device = m_Renderer->GetDevice();
            const auto vertexBytes = static_cast<vk::DeviceSize>(vertices.size() * sizeof(glm::vec3));
            m_GravityGridVertexBuffer = std::make_unique<VulkanBuffer>(
                device, MakeVertexBufferSpecification(vertexBytes, true), m_Logger);
            m_GravityGridVertexBuffer->Upload(vertices.data(), vertexBytes);

            const auto indexBytes = static_cast<vk::DeviceSize>(indices.size() * sizeof(std::uint32_t));
            m_GravityGridIndexBuffer = std::make_unique<VulkanBuffer>(
                device, MakeIndexBufferSpecification(indexBytes, true), m_Logger);
            m_GravityGridIndexBuffer->Upload(indices.data(), indexBytes);
            m_GravityGridIndexCount = static_cast<std::uint32_t>(indices.size());
        }

        void FixupPendingConvexHulls()
        {
            const auto sim = m_Sim.Load();
            if (!sim->Runner) return;
            std::vector<std::pair<std::uint64_t, std::string>> pending;
            {
                const auto sceneLock = sim->Runner->AcquireSnapshot();
                for (auto [entity, mesh, collider, metadata] :
                     sceneLock->Registry()
                         .view<const MeshComponent, const ColliderComponent, const MetadataComponent>()
                         .each())
                {
                    if (collider.Shape == ColliderShape::ConvexMesh && collider.ConvexHullPoints.empty()
                        && !mesh.AssetPath.empty())
                    {
                        pending.emplace_back(metadata.Guid, mesh.AssetPath);
                    }
                }
            }
            for (const auto& [guid, assetPath] : pending)
            {
                const auto gltfScene = m_Meshes->GetOrLoad(assetPath);
                if (!gltfScene || gltfScene->Geometry.Vertices.empty()) continue;
                auto points = gltfScene->Geometry.Vertices;
                sim->Runner->EnqueueEdit([guid, points = std::move(points)](Scene& scene)
                {
                    const auto entity = scene.FindByGuid(guid);
                    if (scene.IsValid(entity) && scene.Registry().all_of<ColliderComponent>(entity))
                    {
                        scene.Registry().get<ColliderComponent>(entity).ConvexHullPoints = points;
                    }
                });
            }
        }

        std::vector<MeshDrawable> CollectMeshDrawables(const GPP::Scene& scene)
        {
            std::vector<MeshDrawable> drawables;
            for (auto [entity, meshComponent, transform] :
                 scene.Registry().view<const MeshComponent, const TransformComponent>().each())
            {
                if (meshComponent.AssetPath.empty()) continue;
                const auto gltfScene = m_Meshes->GetOrLoad(meshComponent.AssetPath);
                if (!gltfScene) continue;
                const glm::mat4 model = transform.GetMatrix();
                for (const auto& mesh : gltfScene->Meshes)
                {
                    for (const auto& primitive : mesh.Primitives)
                    {
                        MeshDrawable drawable;
                        drawable.Primitive = &primitive;
                        drawable.Model = model;
                        if (primitive.MaterialIndex >= 0 && static_cast<std::size_t>(primitive.MaterialIndex)
                                                                 < gltfScene->Materials.size())
                        {
                            const auto& material = gltfScene->Materials[static_cast<std::size_t>(primitive.MaterialIndex)];
                            drawable.BaseColorFactor = material.BaseColorFactor;
                            drawable.MetallicFactor = material.MetallicFactor;
                            drawable.RoughnessFactor = material.RoughnessFactor;
                            drawable.BaseColorTexture = material.BaseColorTexture;
                        }
                        drawables.push_back(std::move(drawable));
                    }
                }
            }
            return drawables;
        }

        void UploadMeshCamera()
        {
            MeshCameraParamsGpu params{};
            params.View = m_RenderCamera.GetViewMatrix();
            params.Projection = m_RenderCamera.GetProjectionMatrix();
            params.CameraPos = m_RenderCamera.GetPosition();
            params.LightDir = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
            m_MeshCameraBuffer->Upload(&params, sizeof(params));
        }

        void UploadGravityGridParams(const GPP::Scene& scene)
        {
            GravityGridParamsGpu params{};
            params.ViewProjection = m_RenderCamera.GetViewProjectionMatrix();
            params.PlaneY = kGravityGridPlaneY;
            params.CellSize = kGravityGridCellSize;
            params.LineThickness = kGravityGridLineThickness;
            params.Opacity = kGravityGridOpacity;
            params.Color = kGravityGridColor;
            FillGravityGridData(params, scene);
            m_GravityGridParamsBuffer->Upload(&params, sizeof(params));
        }

        void EnsureImages(vk::Extent3D extent)
        {
            if (m_ImageExtent.width == extent.width && m_ImageExtent.height == extent.height
                && m_RaytraceImage)
            {
                return;
            }

            const auto device = m_Renderer->GetDevice();
            CreateOrResize(m_RaytraceImage, device, extent, "ViewportRaytrace");
            CreateOrResize(m_BloomBrightImage, device, extent, "ViewportBloomBright");
            CreateOrResize(m_BloomBlurImages[0], device, extent, "ViewportBloomBlur0");
            CreateOrResize(m_BloomBlurImages[1], device, extent, "ViewportBloomBlur1");
            CreateOrResize(m_LensFlareImage, device, extent, "ViewportLensFlare");
            m_ImageExtent = extent;
        }

        void CreateOrResize(std::unique_ptr<VulkanImage>& image, const std::shared_ptr<VulkanDevice>& device,
                            vk::Extent3D extent, std::string debugName)
        {
            if (!image)
            {
                image = std::make_unique<VulkanImage>(device, MakeComputeTargetSpec(extent, std::move(debugName)), m_Logger);
            }
            else
            {
                image->Resize(extent);
            }
            ImmediateSubmit(*m_UploadPool, device->GetBackgroundQueue(), [&](const vk::CommandBuffer cmd)
            {
                TransitionImageLayout(cmd, image->GetImage(), vk::Format::eR32G32B32A32Sfloat,
                                      vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
            });
        }

        [[nodiscard]] std::string StartupScenePath() const
        {
            static constexpr auto kDefaultScenePath = "templates/test-scene.yaml";
            // Explicit --scene (or export request) wins; otherwise resume the last session's
            // scene if one was persisted; otherwise fall back to the bundled default.
            return m_UiState->StartupScenePath.value_or(
                [this]
                {
                    const auto lastScene = m_AppState->GetLastScenePath();
                    return lastScene.empty() ? std::string(kDefaultScenePath) : lastScene;
                }());
        }

        bool BeginSceneLoad(std::string path, const SceneLoadResult::Kind kind)
        {
            bool expected = false;
            if (!m_SceneLoad->InFlight.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                return false;
            }
            try
            {
                ThreadPool::Instance().Submit(
                    [state = m_SceneLoad, scenes = m_Scenes, meshes = m_Meshes, logger = m_Logger,
                     path = std::move(path), kind]() mutable
                    {
                        RunSceneLoad(state, scenes, meshes, logger, std::move(path), kind);
                    });
            }
            catch (const std::exception&)
            {
                // The pool is shutting down together with the application: nothing to load for.
                m_SceneLoad->InFlight.store(false, std::memory_order_release);
                m_SceneLoad->InFlight.notify_all();
                return false;
            }
            return true;
        }

        void CreateDefaultScene(const std::string& name, const std::string& entityName)
        {
            auto& scene = m_Scenes->CreateScene(name);
            const auto entity = scene.CreateEntity(entityName, "BlackHole");
            scene.Registry().emplace<TransformComponent>(entity, TransformComponent{});
            scene.Registry().emplace<BlackHoleComponent>(entity, BlackHoleComponent{.Mass = 1.0f, .Spin = 0.5f});
        }

        void PollSceneLoad()
        {
            std::optional<SceneLoadResult> result;
            {
                std::scoped_lock lock(m_SceneLoad->Mutex);
                result = std::exchange(m_SceneLoad->Result, std::nullopt);
            }
            if (!result) return;

            using Kind = SceneLoadResult::Kind;
            if (result->Ok)
            {
                if (result->LoadKind == Kind::Template)
                {
                    m_UiState->CurrentScenePath.clear();
                }
                else
                {
                    m_UiState->CurrentScenePath = result->Path;
                    m_AppState->NotifySceneOpened(result->Path);
                }
                if (result->LoadKind == Kind::Startup)
                {
                    m_Logger->Info("ViewportLayer: loaded scene '{}'", result->SceneName);
                }
                StartSimulationFor(result->SceneName);
            }
            else if (result->LoadKind == Kind::Startup)
            {
                m_Logger->Warn("ViewportLayer: failed to load '{}' ({}), creating a default scene", result->Path,
                               result->Error);
                CreateDefaultScene("ViewportDefault", "BlackHole");
                m_UiState->CurrentScenePath.clear();
                StartSimulationFor("ViewportDefault");
            }
            else
            {
                m_Logger->Error("ViewportLayer: failed to load scene '{}': {}", result->Path, result->Error);
            }
        }

        void StartSimulationFor(const std::string& sceneName)
        {
            if (const auto previous = m_Sim.Load(); previous->Runner && !m_UiState->CurrentSceneName.empty())
            {
                // Joined on a worker: the old simulation may be in the middle of a long tick.
                m_Scenes->DestroySimulationAsync(m_UiState->CurrentSceneName);
            }

            auto physics = std::make_shared<PhysicsSimulationModule>(m_Dispatcher, m_Logger);
            auto gravity = std::make_shared<GravitySimulationModule>(physics, m_Dispatcher, m_Logger);
            gravity->SetGravityMultiplier(m_UiState->GravityMultiplier);
            SimulationOptions options;
            options.FixedTimestep = std::chrono::duration<float>(1.0f / std::max(m_UiState->SimulationTickRate, 1.0f));
            auto runner = m_Scenes->CreateSimulation(
                sceneName, std::vector<std::shared_ptr<ISimulationModule>>{gravity, physics}, options);
            runner->Start();
            runner->SetPaused(true);
            m_Sim.Publish(SimBinding{runner});
            m_UiState->CurrentSceneName = sceneName;
            m_UiState->SelectedEntityGuid = 0;
            {
                std::scoped_lock lock(m_PathMutex);
                m_ObjectPathTracker.Clear();
            }
        }

        void CheckPendingSceneSwitch()
        {
            if (m_UiState->PendingNewScene.exchange(false))
            {
                const auto name = "Untitled-" + std::to_string(++m_SceneCounter);
                CreateDefaultScene(name, "Black Hole");
                m_UiState->CurrentScenePath.clear();
                StartSimulationFor(name);
            }
            else if (auto path = m_UiState->PendingLoadScenePath.Take())
            {
                if (!BeginSceneLoad(*path, SceneLoadResult::Kind::File))
                    m_UiState->PendingLoadScenePath.Restore(std::move(*path));
            }
            else if (auto path = m_UiState->PendingLoadTemplatePath.Take())
            {
                if (!BeginSceneLoad(*path, SceneLoadResult::Kind::Template))
                    m_UiState->PendingLoadTemplatePath.Restore(std::move(*path));
            }

            const auto sim = m_Sim.Load();
            if (m_UiState->PendingSnapshotForPlay.exchange(false) && sim->Runner)
            {
                const bool fresh = !sim->Play->Active.exchange(true);
                if (fresh)
                {
                    std::scoped_lock lock(m_PathMutex);
                    m_ObjectPathTracker.Clear();
                }
                sim->Runner->EnqueueTrackedEdit([play = sim->Play, runner = sim->Runner.get(), fresh](Scene& scene)
                {
                    if (fresh)
                    {
                        std::scoped_lock lock(play->Mutex);
                        play->Snapshot = scene.Clone();
                    }
                    runner->SetPaused(false);
                });
            }

            if (m_UiState->PendingStopSimulation.exchange(false) && sim->Runner)
            {
                sim->Play->Active = false;
                sim->Runner->EnqueueEdit([play = sim->Play, runner = sim->Runner.get()](Scene& scene)
                {
                    std::optional<Scene> snapshot;
                    {
                        std::scoped_lock lock(play->Mutex);
                        snapshot = std::exchange(play->Snapshot, std::nullopt);
                    }
                    if (snapshot) Scene::SyncInto(*snapshot, scene);
                    runner->SetPaused(true);
                });
            }
        }

        struct ExportJob
        {
            std::atomic<int> WritesInFlight{0};
            std::atomic<int> FramesWritten{0};
            std::atomic<bool> WriteFailed{false};
            std::atomic<bool> EncodeFinished{false};
            std::atomic<bool> EncodeFailed{false};
        };

        void ProcessExport()
        {
            if (m_PendingCapture)
            {
                m_PendingCapture = false;
                CaptureExportFrame();
            }

            if (!m_UiState->ExportActive)
            {
                if (auto request = m_UiState->PendingExport.Take()) StartExport(*request);
            }

            if (m_UiState->ExportActive)
            {
                m_Renderer->ResizeBufferTarget(m_LayerTarget.Id, m_UiState->ExportResolution);

                if (m_ExportAllCaptured)
                {
                    PollExportCompletion();
                }
                else if (m_ExportPipelinesReady.load(std::memory_order_acquire))
                {
                    m_PendingCapture = true;
                }
                else
                {
                    m_UiState->ExportStatus = "Compiling shaders...";
                }
            }
        }

        [[nodiscard]] bool AllExportPipelinesReady() const
        {
            return m_RaytracePipeline && m_RaytracePipeline->GetPipeline() &&
                   m_BloomExtractPipeline && m_BloomExtractPipeline->GetPipeline() &&
                   m_BloomBlurPipeline && m_BloomBlurPipeline->GetPipeline() &&
                   m_LensFlarePipeline && m_LensFlarePipeline->GetPipeline() &&
                   m_CompositePipeline && m_CompositePipeline->GetPipeline();
        }

        void StartExport(const ExportRequest& request)
        {
            m_ExportRequest = request;
            m_UiState->ExportResolution = {request.Width, request.Height};
            m_UiState->ExportActive = true;
            m_UiState->ExportProgress = 0.0f;
            m_UiState->ExportStatus = "Starting export...";
            m_ExportFrameIndex = 0;
            m_ExportTotalFrames = request.RequestKind == ExportRequest::Kind::Video
                                      ? std::max(1, static_cast<int>(request.DurationSeconds * request.Framerate))
                                      : 1;
            m_ExportAllCaptured = false;
            m_ExportEncodeStarted = false;
            m_ExportJob = std::make_shared<ExportJob>();

            m_SavedRenderToggles = m_UiState->Render;
            if (request.RayStepSize) m_UiState->Render.RayStepSize = *request.RayStepSize;
            if (request.MaxRaySteps) m_UiState->Render.MaxRaySteps = *request.MaxRaySteps;

            const auto stats = m_Renderer->GetBufferTargetStats(m_LayerTarget.Id);
            m_ExportMinSerial = (stats ? stats->FramesRendered : 0) + 3;

            if (request.RequestKind == ExportRequest::Kind::Video)
            {
                m_ExportTempDir = std::filesystem::temp_directory_path() /
                                  std::format("molehole_export_{}",
                                             std::chrono::steady_clock::now().time_since_epoch().count());
                std::filesystem::create_directories(m_ExportTempDir);
            }

            m_Logger->Info("Export started: {}x{} -> {}", request.Width, request.Height, request.OutputPath);
        }

        void CaptureExportFrame()
        {
            if (m_ExportJob->WritesInFlight.load() >= 2)
            {
                m_PendingCapture = true;
                return;
            }

            auto readback = m_Renderer->ReadBackBufferTarget(m_LayerTarget.Id, m_ExportMinSerial,
                                                             m_UiState->ExportResolution);
            if (readback.Pixels.empty())
            {
                m_PendingCapture = true;
                return;
            }
            m_ExportMinSerial = readback.Serial + 1;

            const auto path = m_ExportRequest.RequestKind == ExportRequest::Kind::Image
                                  ? std::filesystem::path(m_ExportRequest.OutputPath)
                                  : m_ExportTempDir / std::format("frame_{:06d}.png", m_ExportFrameIndex);
            ++m_ExportFrameIndex;

            m_ExportJob->WritesInFlight.fetch_add(1);
            auto write = [job = m_ExportJob, logger = m_Logger, path,
                          readback = std::move(readback)]() mutable
            {
                const bool bgr = readback.Format == vk::Format::eB8G8R8A8Unorm ||
                                readback.Format == vk::Format::eB8G8R8A8Srgb;
                const auto pixelCount = static_cast<std::size_t>(readback.Extent.width) * readback.Extent.height;
                std::vector<unsigned char> rgb(pixelCount * 3);
                for (std::size_t i = 0; i < pixelCount; ++i)
                {
                    const auto* p = &readback.Pixels[i * 4];
                    rgb[i * 3 + 0] = bgr ? p[2] : p[0];
                    rgb[i * 3 + 1] = p[1];
                    rgb[i * 3 + 2] = bgr ? p[0] : p[2];
                }
                if (!stbi_write_png(path.string().c_str(), static_cast<int>(readback.Extent.width),
                                    static_cast<int>(readback.Extent.height), 3, rgb.data(),
                                    static_cast<int>(readback.Extent.width) * 3))
                {
                    logger->Error("ViewportLayer: failed to write '{}'", path.string());
                    job->WriteFailed = true;
                }
                job->FramesWritten.fetch_add(1);
                job->WritesInFlight.fetch_sub(1);
            };
            try
            {
                ThreadPool::Instance().Submit(std::move(write));
            }
            catch (const std::exception&)
            {
                m_ExportJob->WritesInFlight.fetch_sub(1);
                m_ExportJob->WriteFailed = true;
            }

            m_UiState->ExportStatus =
                m_ExportRequest.RequestKind == ExportRequest::Kind::Video
                    ? std::format("Rendering frame {}/{}", m_ExportFrameIndex, m_ExportTotalFrames)
                    : "Rendering...";

            if (m_ExportFrameIndex >= m_ExportTotalFrames)
            {
                m_ExportAllCaptured = true;
            }
            else
            {
                m_PendingCapture = true;
            }
        }

        void PollExportCompletion()
        {
            const int written = m_ExportJob->FramesWritten.load();
            m_UiState->ExportProgress = static_cast<float>(written) / static_cast<float>(m_ExportTotalFrames);

            if (written < m_ExportTotalFrames && m_ExportJob->WritesInFlight.load() > 0)
            {
                m_UiState->ExportStatus = std::format("Writing frames {}/{}", written, m_ExportTotalFrames);
                return;
            }

            if (m_ExportRequest.RequestKind == ExportRequest::Kind::Video)
            {
                if (!m_ExportEncodeStarted)
                {
                    m_ExportEncodeStarted = true;
                    m_UiState->ExportStatus = "Encoding video...";
                    auto encode = [job = m_ExportJob, logger = m_Logger, framerate = m_ExportRequest.Framerate,
                                   frames = (m_ExportTempDir / "frame_%06d.png").string(),
                                   output = m_ExportRequest.OutputPath, tempDir = m_ExportTempDir]
                    {
                        const auto cmd = std::format(
                            "ffmpeg -y -framerate {} -i {} -c:v libx264 -pix_fmt yuv420p {} > /dev/null 2>&1",
                            framerate, frames, output);
                        if (std::system(cmd.c_str()) != 0)
                        {
                            logger->Error("ViewportLayer: ffmpeg encoding failed (is ffmpeg installed?)");
                            job->EncodeFailed = true;
                        }
                        std::error_code ec;
                        std::filesystem::remove_all(tempDir, ec);
                        job->EncodeFinished = true;
                    };
                    try
                    {
                        ThreadPool::Instance().Submit(std::move(encode));
                    }
                    catch (const std::exception&)
                    {
                        m_ExportJob->EncodeFailed = true;
                        m_ExportJob->EncodeFinished = true;
                    }
                    return;
                }
                if (!m_ExportJob->EncodeFinished.load())
                {
                    return;
                }
            }

            FinishExport();
        }

        void FinishExport()
        {
            m_UiState->Render = m_SavedRenderToggles;

            const bool failed = m_ExportJob->EncodeFailed.load() || m_ExportJob->WriteFailed.load();
            m_UiState->ExportActive = false;
            m_UiState->ExportProgress = 1.0f;
            if (failed)
            {
                m_UiState->ExportStatus = m_ExportJob->EncodeFailed.load() ? "Failed: ffmpeg encoding error"
                                                                           : "Failed: could not write image";
            }
            else
            {
                m_UiState->ExportStatus = "Complete";
                m_Logger->Info("Export complete: {}", m_ExportRequest.OutputPath);
                m_AppState->NotifyExported(m_ExportRequest.OutputPath);
            }

            if (m_UiState->ExitWhenExportDone)
            {
                Application::Instance().Stop();
            }
        }

        void UpdateCamera(float deltaTime)
        {
            if (m_UiState->IntroActive) return;

            m_Camera.SetPosition(m_UiState->CameraPosition);
            m_Camera.SetYawPitch(m_UiState->CameraYaw, m_UiState->CameraPitch);
            m_Camera.SetFov(m_UiState->CameraFov);

            float forward = 0.0f, right = 0.0f, up = 0.0f;
            if (m_Input->IsKeyDown(KeyCode::W)) forward += 1.0f;
            if (m_Input->IsKeyDown(KeyCode::S)) forward -= 1.0f;
            if (m_Input->IsKeyDown(KeyCode::D)) right += 1.0f;
            if (m_Input->IsKeyDown(KeyCode::A)) right -= 1.0f;
            if (m_Input->IsKeyDown(KeyCode::E)) up += 1.0f;
            if (m_Input->IsKeyDown(KeyCode::Q)) up -= 1.0f;
            if (forward != 0.0f || right != 0.0f || up != 0.0f)
            {
                m_Camera.ProcessKeyboard(forward, right, up, deltaTime, m_UiState->CameraSpeed);
            }

            const float mouseX = m_Input->MouseX();
            const float mouseY = m_Input->MouseY();
            if (m_Input->IsMouseButtonDown(MouseButton::Right))
            {
                if (m_HasLastMouse)
                {
                    const float dx = mouseX - m_LastMouseX;
                    const float dy = m_LastMouseY - mouseY;
                    m_Camera.ProcessMouse(dx, dy, m_UiState->CameraMouseSensitivity);
                }
                m_HasLastMouse = true;
            }
            else
            {
                m_HasLastMouse = false;
            }
            m_LastMouseX = mouseX;
            m_LastMouseY = mouseY;

            m_UiState->CameraPosition = m_Camera.GetPosition();
            m_UiState->CameraYaw = m_Camera.GetYaw();
            m_UiState->CameraPitch = m_Camera.GetPitch();
        }

        void UploadParams(vk::Extent3D extent, const GPP::Scene& scene)
        {
            RaytraceParamsGpu params{};
            params.CameraPos = m_RenderCamera.GetPosition();
            params.CameraFront = m_RenderCamera.GetFront();
            params.CameraUp = m_RenderCamera.GetUp();
            params.CameraRight = m_RenderCamera.GetRight();
            params.Fov = m_RenderCamera.GetFov();
            params.Aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
            params.Time = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_StartTime).count();
            params.ViewProjection = m_RenderCamera.GetViewProjectionMatrix();
            FillSceneData(params, scene);

            const auto& render = m_RenderToggles;
            params.DebugMode = render.DebugMode;
            params.MetricType = render.MetricType;
            params.IsPhysicallyAccurate = render.PhysicallyAccurate ? 1 : 0;
            params.GravitationalLensingEnabled = render.GravitationalLensing ? 1 : 0;
            params.GravitationalRedshiftEnabled = render.GravitationalRedshift ? 1 : 0;
            params.AccretionDiskEnabled = render.AccretionDisk ? 1 : 0;
            params.AccretionDiskVolumetric = render.AccretionDiskVolumetric ? 1 : 0;
            params.RenderBlackHoles = render.RenderBlackHoles ? 1 : 0;
            params.RenderSpheres = render.RenderSpheres ? 1 : 0;
            params.DopplerBeamingEnabled = render.DopplerBeaming ? 1.0f : 0.0f;
            params.AccDiskHeight = render.AccDiskHeight;
            params.AccDiskSpeed = render.AccDiskSpeed;
            params.AccDiskNoiseScale = render.AccDiskNoiseScale;
            params.AccDiskNoiseLOD = render.AccDiskNoiseLOD;
            params.RayStepSize = render.RayStepSize;
            params.MaxRaySteps = render.MaxRaySteps;
            params.AdaptiveStepRate = render.AdaptiveStepRate;

            m_ParamsBuffer->Upload(&params, sizeof(params));
        }

        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<EventDispatcher> m_Dispatcher;
        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<InputState> m_Input;
        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<AppStateService> m_AppState;
        std::shared_ptr<MeshCache> m_Meshes;
        std::shared_ptr<SceneLoadState> m_SceneLoad;

        LatestValue<ViewportFrameParams> m_FrameParams; // UI -> viewport: camera + render toggles
        LatestValue<SimBinding> m_Sim;                  // UI -> viewport: which simulation to draw

        std::mutex m_PathMutex;
        ObjectPathTracker m_ObjectPathTracker;
        std::atomic<bool> m_ExportPipelinesReady{false}; // viewport -> UI

        // ---- UI thread only ----
        bool m_PendingCapture{false};
        ExportRequest m_ExportRequest;
        int m_ExportFrameIndex{0};
        int m_ExportTotalFrames{0};
        bool m_ExportAllCaptured{false};
        bool m_ExportEncodeStarted{false};
        std::uint64_t m_ExportMinSerial{0};
        std::shared_ptr<ExportJob> m_ExportJob = std::make_shared<ExportJob>();
        std::filesystem::path m_ExportTempDir;
        RenderToggles m_SavedRenderToggles;

        int m_SceneCounter{0};
        Camera m_Camera;
        bool m_HasLastMouse{false};
        float m_LastMouseX{0.0f};
        float m_LastMouseY{0.0f};

        // viewport thread only
        Camera m_RenderCamera;
        RenderToggles m_RenderToggles;
        std::shared_ptr<PhysicsSimulationModule> m_RenderPhysics;
        std::chrono::steady_clock::time_point m_StartTime;

        std::unique_ptr<VulkanCommandPool> m_UploadPool;
        std::unique_ptr<VulkanBuffer> m_ParamsBuffer;

        std::shared_ptr<ShaderPipeline> m_RaytracePipeline;
        std::shared_ptr<ShaderPipeline> m_BloomExtractPipeline;
        std::shared_ptr<ShaderPipeline> m_BloomBlurPipeline;
        std::shared_ptr<ShaderPipeline> m_LensFlarePipeline;
        std::shared_ptr<ShaderPipeline> m_CompositePipeline;
        vk::Format m_CompositeColorFormat{vk::Format::eUndefined};
        vk::Format m_CompositeDepthFormat{vk::Format::eUndefined};

        vk::Extent3D m_ImageExtent{0, 0, 0};
        std::unique_ptr<VulkanImage> m_RaytraceImage;
        std::unique_ptr<VulkanImage> m_BloomBrightImage;
        std::array<std::unique_ptr<VulkanImage>, 2> m_BloomBlurImages;
        std::unique_ptr<VulkanImage> m_LensFlareImage;

        std::shared_ptr<VulkanImage> m_BlackbodyLut;
        std::shared_ptr<VulkanImage> m_AccelerationLut;
        std::shared_ptr<VulkanImage> m_HrDiagramLut;
        std::shared_ptr<VulkanImage> m_Skybox;
        std::shared_ptr<VulkanImage> m_DummyWhiteTexture;

        std::shared_ptr<ShaderPipeline> m_MeshPipeline;
        vk::Format m_MeshColorFormat{vk::Format::eUndefined};
        vk::Format m_MeshDepthFormat{vk::Format::eUndefined};
        std::unique_ptr<VulkanBuffer> m_MeshCameraBuffer;

        std::shared_ptr<ShaderPipeline> m_GravityGridPipeline;
        vk::Format m_GravityGridColorFormat{vk::Format::eUndefined};

        std::shared_ptr<ShaderPipeline> m_PhysicsDebugPipeline;
        vk::Format m_PhysicsDebugColorFormat{vk::Format::eUndefined};
        std::unique_ptr<VulkanBuffer> m_PhysicsDebugVertexBuffer;
        std::uint32_t m_PhysicsDebugLineVertexCount{0};
        std::unique_ptr<VulkanBuffer> m_ObjectPathVertexBuffer;
        std::uint32_t m_ObjectPathLineVertexCount{0};
        std::unique_ptr<VulkanBuffer> m_GravityGridVertexBuffer;
        std::unique_ptr<VulkanBuffer> m_GravityGridIndexBuffer;
        std::uint32_t m_GravityGridIndexCount{0};
        std::unique_ptr<VulkanBuffer> m_GravityGridParamsBuffer;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(ViewportLayer)
