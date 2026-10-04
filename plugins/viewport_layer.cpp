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

    // raytrace -> bloom extract -> bloom blur (ping-pong) -> lens flare -> composite
    // -> gravity-grid overlay -> mesh overlay
    // not ported yet: Object-path trails and PhysX debug lines until later
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
              m_Dispatcher(dispatcher), m_Scenes(scenes), m_Input(input), m_UiState(uiState), m_AppState(appState)
        {
        }

        void OnAttach() override
        {
            MoleHole::RegisterComponents();
            const auto sceneName = LoadScene();
            StartSimulationFor(sceneName);

            const auto device = m_Renderer->GetDevice();
            m_UploadPool = std::make_unique<VulkanCommandPool>(device, m_Logger, device->GetQueueIndices().Graphics);
            const auto queue = device->GetGraphicsQueue();

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
            if (m_Runner) m_Runner->Stop();
            m_Logger->Info("ViewportLayer detached");
        }

        void OnUpdate(float deltaTime) override
        {
            CheckPendingSceneSwitch();
            UpdateCamera(deltaTime);
            ProcessExport();
        }

        void OnRenderGraph(RenderGraph& graph) override
        {
            const auto colorTarget = graph.GetPrimaryColorTarget();
            if (colorTarget == kInvalidRenderGraphHandle || !m_RaytracePipeline || !m_Runner) return;
            const auto extent = graph.GetImageExtent(colorTarget);
            if (extent.width == 0 || extent.height == 0) return;

            auto sceneLock = m_Runner->LockRenderScene();
            Scene& scene = *sceneLock;

            EnsureImages(extent);
            EnsureCompositePipeline(graph.GetImageFormat(colorTarget));
            UploadParams(extent, scene);

            const auto device = m_Renderer->GetDevice();
            const std::uint32_t groupsX = (extent.width + 15u) / 16u;
            const std::uint32_t groupsY = (extent.height + 15u) / 16u;

            graph.AddComputePass(
                "ViewportLayer.Raytrace", {}, {},
                [this, device, groupsX, groupsY](const vk::CommandBuffer cmd, RenderGraph& g)
                {
                    const auto pipeline = m_RaytracePipeline->GetPipeline();
                    if (!pipeline) return;
                    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->GetPipeline());
                    const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                    DescriptorSetWriter(device->GetDevice())
                        .WriteStorageImage(set, 0, m_RaytraceImage->GetImageView())
                        .WriteUniformBuffer(set, 1, m_ParamsBuffer->GetBuffer())
                        .WriteCombinedImageSampler(set, 2, m_Skybox->GetImageView(), m_Skybox->GetSampler())
                        .WriteCombinedImageSampler(set, 3, m_BlackbodyLut->GetImageView(), m_BlackbodyLut->GetSampler())
                        .WriteCombinedImageSampler(set, 4, m_AccelerationLut->GetImageView(), m_AccelerationLut->GetSampler())
                        .WriteCombinedImageSampler(set, 5, m_HrDiagramLut->GetImageView(), m_HrDiagramLut->GetSampler())
                        .Update();
                    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->GetLayout(), 0, set, {});
                    cmd.dispatch(groupsX, groupsY, 1);
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
                graph.AddGraphicsPass(
                    "ViewportLayer.Composite", {}, {},
                    {RenderGraphAttachment{
                        .Handle = colorTarget, .LoadOp = vk::AttachmentLoadOp::eClear,
                        .Clear = vk::ClearValue(vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f))
                    }},
                    std::nullopt,
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

            if (m_UiState->Render.ShowGravityGrid)
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

            if (const auto depthTarget = graph.GetPrimaryDepthTarget(); depthTarget != kInvalidRenderGraphHandle)
            {
                EnsureMeshPipeline(graph.GetImageFormat(colorTarget), graph.GetImageFormat(depthTarget));
                auto drawables = CollectMeshDrawables(scene);
                if (m_MeshPipeline && !drawables.empty())
                {
                    UploadMeshCamera();
                    graph.AddGraphicsPass(
                        "ViewportLayer.Mesh", {}, {},
                        {RenderGraphAttachment{.Handle = colorTarget, .LoadOp = vk::AttachmentLoadOp::eLoad}},
                        RenderGraphAttachment{
                            .Handle = depthTarget, .LoadOp = vk::AttachmentLoadOp::eClear,
                            .Clear = vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0))
                        },
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
            ImGui::Begin("Viewport");

            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (!m_UiState->ExportActive && avail.x >= 1.0f && avail.y >= 1.0f)
            {
                m_Renderer->ResizeBufferTarget(
                    m_LayerTarget.Id,
                    glm::uvec2{static_cast<std::uint32_t>(avail.x), static_cast<std::uint32_t>(avail.y)});
            }

            if (const auto target = m_Renderer->GetRenderTargetInfo(m_LayerTarget.Id))
            {
                ImGui::Image(reinterpret_cast<ImTextureID>(target->ImGuiTexture), avail);
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                m_UiState->ViewportScreenMin = {min.x, min.y};
                m_UiState->ViewportScreenMax = {max.x, max.y};
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

        void EnsureCompositePipeline(vk::Format colorFormat)
        {
            if (m_CompositePipeline && m_CompositeColorFormat == colorFormat) return;
            const auto device = m_Renderer->GetDevice();
            m_CompositePipeline = std::make_shared<ShaderPipeline>(
                device,
                VulkanPipelineSpecification{
                    .colorFormat = colorFormat,
                    .depthFormat = vk::Format::eUndefined,
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

        std::shared_ptr<GltfSceneData> GetOrLoadMesh(const std::string& assetPath)
        {
            if (const auto it = m_MeshCache.find(assetPath); it != m_MeshCache.end())
            {
                return it->second;
            }
            std::shared_ptr<GltfSceneData> result;
            try
            {
                result = std::make_shared<GltfSceneData>(
                    LoadGltfScene(m_Renderer->GetDevice(), m_FileSystem, assetPath, m_Logger));
            }
            catch (const std::exception& error)
            {
                m_Logger->Warn("ViewportLayer: failed to load mesh '{}': {}", assetPath, error.what());
            }
            m_MeshCache.emplace(assetPath, result);
            return result;
        }

        std::vector<MeshDrawable> CollectMeshDrawables(GPP::Scene& scene)
        {
            std::vector<MeshDrawable> drawables;
            for (auto [entity, meshComponent, transform] :
                 scene.Registry().view<const MeshComponent, const TransformComponent>().each())
            {
                if (meshComponent.AssetPath.empty()) continue;
                const auto gltfScene = GetOrLoadMesh(meshComponent.AssetPath);
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
            params.View = m_Camera.GetViewMatrix();
            params.Projection = m_Camera.GetProjectionMatrix();
            params.CameraPos = m_Camera.GetPosition();
            params.LightDir = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
            m_MeshCameraBuffer->Upload(&params, sizeof(params));
        }

        void UploadGravityGridParams(GPP::Scene& scene)
        {
            GravityGridParamsGpu params{};
            params.ViewProjection = m_Camera.GetViewProjectionMatrix();
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
            ImmediateSubmit(*m_UploadPool, device->GetGraphicsQueue(), [&](const vk::CommandBuffer cmd)
            {
                TransitionImageLayout(cmd, image->GetImage(), vk::Format::eR32G32B32A32Sfloat,
                                      vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
            });
        }

        std::string LoadScene()
        {
            static constexpr auto kDefaultScenePath = "templates/test-scene.yaml";
            // Explicit --scene (or export request) wins; otherwise resume the last session's
            // scene if one was persisted; otherwise fall back to the bundled default.
            const auto scenePath = m_UiState->StartupScenePath.value_or(
                [this]
                {
                    const auto lastScene = m_AppState->GetLastScenePath();
                    return lastScene.empty() ? std::string(kDefaultScenePath) : lastScene;
                }());
            try
            {
                auto& scene = m_Scenes->LoadSceneFromFile(scenePath);
                m_Logger->Info("ViewportLayer: loaded scene '{}'", scene.Metadata().Name);
                m_UiState->CurrentScenePath = scenePath;
                m_AppState->NotifySceneOpened(scenePath);
                return scene.Metadata().Name;
            }
            catch (const std::exception& error)
            {
                m_Logger->Warn("ViewportLayer: failed to load '{}' ({}), creating a default scene",
                               scenePath, error.what());
                auto& scene = m_Scenes->CreateScene("ViewportDefault");
                const auto entity = scene.CreateEntity("BlackHole", "BlackHole");
                scene.Registry().emplace<TransformComponent>(entity, TransformComponent{});
                scene.Registry().emplace<BlackHoleComponent>(entity, BlackHoleComponent{.Mass = 1.0f, .Spin = 0.5f});
                m_UiState->CurrentScenePath.clear();
                return scene.Metadata().Name;
            }
        }

        void StartSimulationFor(const std::string& sceneName)
        {
            if (m_Runner && !m_UiState->CurrentSceneName.empty())
            {
                m_Scenes->DestroySimulation(m_UiState->CurrentSceneName);
            }
            auto physicsModule = std::make_shared<PhysicsSimulationModule>(m_Dispatcher, m_Logger);
            auto gravityModule = std::make_shared<GravitySimulationModule>(physicsModule, m_Dispatcher, m_Logger);
            m_Runner = m_Scenes->CreateSimulation(
                sceneName, std::vector<std::shared_ptr<ISimulationModule>>{gravityModule, physicsModule});
            m_Runner->Start();
            m_UiState->CurrentSceneName = sceneName;
            m_UiState->SelectedEntityGuid = 0;
            if (m_UiState->PendingStartPaused)
            {
                m_Runner->SetPaused(true);
                m_UiState->PendingStartPaused = false;
            }
        }

        void CheckPendingSceneSwitch()
        {
            if (m_UiState->PendingNewScene)
            {
                m_UiState->PendingNewScene = false;
                auto& scene = m_Scenes->CreateScene("Untitled-" + std::to_string(++m_SceneCounter));
                const auto entity = scene.CreateEntity("Black Hole", "BlackHole");
                scene.Registry().emplace<TransformComponent>(entity, TransformComponent{});
                scene.Registry().emplace<BlackHoleComponent>(entity, BlackHoleComponent{.Mass = 1.0f, .Spin = 0.5f});
                m_UiState->CurrentScenePath.clear();
                StartSimulationFor(scene.Metadata().Name);
            }
            else if (m_UiState->PendingLoadScenePath)
            {
                const auto path = *m_UiState->PendingLoadScenePath;
                m_UiState->PendingLoadScenePath.reset();
                try
                {
                    auto& scene = m_Scenes->LoadSceneFromFile(path);
                    m_UiState->CurrentScenePath = path;
                    m_AppState->NotifySceneOpened(path);
                    StartSimulationFor(scene.Metadata().Name);
                }
                catch (const std::exception& error)
                {
                    m_Logger->Error("ViewportLayer: failed to load scene '{}': {}", path, error.what());
                }
            }
        }

        void ProcessExport()
        {
            if (m_PendingCapture)
            {
                m_PendingCapture = false;
                CaptureExportFrame();
            }

            if (m_UiState->PendingExport && !m_UiState->ExportActive)
            {
                StartExport(*m_UiState->PendingExport);
                m_UiState->PendingExport.reset();
            }

            if (m_UiState->ExportActive)
            {
                m_Renderer->ResizeBufferTarget(m_LayerTarget.Id, m_UiState->ExportResolution);
                // Shader compilation (StartOnRenderThread) is asynchronous -- GetPipeline() stays
                // null until a background compile finishes and swaps it in. Windowed use always
                // has plenty of real time for that before anyone looks at the viewport; headless
                // export can reach its first capture within a tick or two of OnAttach, racing
                // ahead of it. Without this check we'd silently capture however many frames
                // render before compilation finishes -- compute/graphics passes that find a null
                // pipeline just skip their dispatch (see OnRenderGraph), leaving that pass's
                // image untouched (black for the raytrace image, since EnsureImages never clears
                // it to anything else).
                if (AllExportPipelinesReady())
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

            m_SavedRenderToggles = m_UiState->Render;
            if (request.RayStepSize) m_UiState->Render.RayStepSize = *request.RayStepSize;
            if (request.MaxRaySteps) m_UiState->Render.MaxRaySteps = *request.MaxRaySteps;

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
            const auto readback = m_Renderer->ReadBackBufferTarget(m_LayerTarget.Id);
            if (readback.Pixels.empty() ||
                readback.Extent.width != m_UiState->ExportResolution.x ||
                readback.Extent.height != m_UiState->ExportResolution.y)
            {
                m_PendingCapture = true;
                return;
            }

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

            const auto path = m_ExportRequest.RequestKind == ExportRequest::Kind::Image
                                  ? std::filesystem::path(m_ExportRequest.OutputPath)
                                  : m_ExportTempDir / std::format("frame_{:06d}.png", m_ExportFrameIndex);
            stbi_write_png(path.string().c_str(), static_cast<int>(readback.Extent.width),
                          static_cast<int>(readback.Extent.height), 3, rgb.data(),
                          static_cast<int>(readback.Extent.width) * 3);

            ++m_ExportFrameIndex;
            m_UiState->ExportProgress = static_cast<float>(m_ExportFrameIndex) / static_cast<float>(m_ExportTotalFrames);
            m_UiState->ExportStatus =
                m_ExportRequest.RequestKind == ExportRequest::Kind::Video
                    ? std::format("Rendering frame {}/{}", m_ExportFrameIndex, m_ExportTotalFrames)
                    : "Rendering...";

            if (m_ExportFrameIndex >= m_ExportTotalFrames)
            {
                FinishExport();
            }
            else
            {
                m_PendingCapture = true;
            }
        }

        void FinishExport()
        {
            m_UiState->Render = m_SavedRenderToggles;

            if (m_ExportRequest.RequestKind == ExportRequest::Kind::Video)
            {
                m_UiState->ExportStatus = "Encoding video...";
                const auto cmd = std::format(
                    "ffmpeg -y -framerate {} -i {} -c:v libx264 -pix_fmt yuv420p {} > /dev/null 2>&1",
                    m_ExportRequest.Framerate, (m_ExportTempDir / "frame_%06d.png").string(),
                    m_ExportRequest.OutputPath);
                if (std::system(cmd.c_str()) != 0)
                {
                    m_Logger->Error("ViewportLayer: ffmpeg encoding failed (is ffmpeg installed?)");
                    m_UiState->ExportStatus = "Failed: ffmpeg encoding error";
                }
                std::error_code ec;
                std::filesystem::remove_all(m_ExportTempDir, ec);
            }

            m_UiState->ExportActive = false;
            m_UiState->ExportProgress = 1.0f;
            if (m_UiState->ExportStatus.find("Failed") == std::string::npos)
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

        void UploadParams(vk::Extent3D extent, GPP::Scene& scene)
        {
            RaytraceParamsGpu params{};
            params.CameraPos = m_Camera.GetPosition();
            params.CameraFront = m_Camera.GetFront();
            params.CameraUp = m_Camera.GetUp();
            params.CameraRight = m_Camera.GetRight();
            params.Fov = m_Camera.GetFov();
            params.Aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
            params.Time = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_StartTime).count();
            FillSceneData(params, scene);

            const auto& render = m_UiState->Render;
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

        bool m_PendingCapture{false};
        ExportRequest m_ExportRequest;
        int m_ExportFrameIndex{0};
        int m_ExportTotalFrames{0};
        std::filesystem::path m_ExportTempDir;
        RenderToggles m_SavedRenderToggles;

        std::shared_ptr<SimulationRunner> m_Runner;
        int m_SceneCounter{0};
        Camera m_Camera;
        bool m_HasLastMouse{false};
        float m_LastMouseX{0.0f};
        float m_LastMouseY{0.0f};
        std::chrono::steady_clock::time_point m_StartTime;

        std::unique_ptr<VulkanCommandPool> m_UploadPool;
        std::unique_ptr<VulkanBuffer> m_ParamsBuffer;

        std::shared_ptr<ShaderPipeline> m_RaytracePipeline;
        std::shared_ptr<ShaderPipeline> m_BloomExtractPipeline;
        std::shared_ptr<ShaderPipeline> m_BloomBlurPipeline;
        std::shared_ptr<ShaderPipeline> m_LensFlarePipeline;
        std::shared_ptr<ShaderPipeline> m_CompositePipeline;
        vk::Format m_CompositeColorFormat{vk::Format::eUndefined};

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
        std::unordered_map<std::string, std::shared_ptr<GltfSceneData>> m_MeshCache;

        std::shared_ptr<ShaderPipeline> m_GravityGridPipeline;
        vk::Format m_GravityGridColorFormat{vk::Format::eUndefined};
        std::unique_ptr<VulkanBuffer> m_GravityGridVertexBuffer;
        std::unique_ptr<VulkanBuffer> m_GravityGridIndexBuffer;
        std::uint32_t m_GravityGridIndexCount{0};
        std::unique_ptr<VulkanBuffer> m_GravityGridParamsBuffer;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(ViewportLayer)
