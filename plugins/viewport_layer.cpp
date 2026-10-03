import GPP;
import MoleHole;
import vulkan;
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

    vk::ImageUsageFlags StorageSampledUsage()
    {
        return vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
    }

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

    // Phase 3 renderer: raytrace -> bloom extract -> bloom blur (ping-pong) -> lens flare -> composite,
    // ported from legacy/Renderer/BlackHoleRenderer.cpp onto GPP's render graph. Mesh/grid/object-path/
    // physics-debug overlays and the Kerr geodesic LUTs are not yet ported (see docs/ for the follow-up).
    struct ViewportLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, Renderer, IFileSystem, EventDispatcher, SceneManager, InputState>;

        ViewportLayer(const std::shared_ptr<Logger>& logger,
                      const std::shared_ptr<Renderer>& renderer,
                      const std::shared_ptr<IFileSystem>& fileSystem,
                      const std::shared_ptr<EventDispatcher>& dispatcher,
                      const std::shared_ptr<SceneManager>& scenes,
                      const std::shared_ptr<InputState>& input)
            : HotReloadableLayer(logger), m_Renderer(renderer), m_FileSystem(fileSystem),
              m_Dispatcher(dispatcher), m_Scenes(scenes), m_Input(input)
        {
        }

        void OnAttach() override
        {
            MoleHole::RegisterComponents();
            LoadScene();

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

            m_StartTime = std::chrono::steady_clock::now();
            m_Logger->Info("ViewportLayer attached");
        }

        void OnDetach() override
        {
            m_Logger->Info("ViewportLayer detached");
        }

        void OnUpdate(float deltaTime) override
        {
            UpdateCamera(deltaTime);
        }

        void OnRenderGraph(RenderGraph& graph) override
        {
            const auto colorTarget = graph.GetPrimaryColorTarget();
            if (colorTarget == kInvalidRenderGraphHandle || !m_RaytracePipeline) return;
            const auto extent = graph.GetImageExtent(colorTarget);
            if (extent.width == 0 || extent.height == 0) return;

            EnsureImages(extent);
            EnsureCompositePipeline(graph.GetImageFormat(colorTarget));
            UploadParams(extent);

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
        }

        void OnUiRender() override
        {
            ImGui::Begin("Viewport");
            if (const auto target = m_Renderer->GetRenderTargetInfo(m_LayerTarget.Id))
            {
                ImGui::Image(
                    reinterpret_cast<ImTextureID>(target->ImGuiTexture),
                    ImVec2(static_cast<float>(target->Extent.width),
                           static_cast<float>(target->Extent.height)));
            }
            else
            {
                ImGui::TextUnformatted("Viewport buffer not ready yet.");
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

        void LoadScene()
        {
            static constexpr auto kDefaultScenePath = "templates/test-scene.yaml";
            try
            {
                m_Scene = &m_Scenes->LoadSceneFromFile(kDefaultScenePath);
                m_Logger->Info("ViewportLayer: loaded scene '{}'", m_Scene->Metadata().Name);
            }
            catch (const std::exception& error)
            {
                m_Logger->Warn("ViewportLayer: failed to load '{}' ({}), creating a default scene",
                               kDefaultScenePath, error.what());
                m_Scene = &m_Scenes->CreateScene("ViewportDefault");
                const auto entity = m_Scene->CreateEntity("BlackHole", "BlackHole");
                m_Scene->Registry().emplace<TransformComponent>(entity, TransformComponent{});
                m_Scene->Registry().emplace<BlackHoleComponent>(entity, BlackHoleComponent{.Mass = 1.0f, .Spin = 0.5f});
            }
        }

        void UpdateCamera(float deltaTime)
        {
            float forward = 0.0f, right = 0.0f, up = 0.0f;
            if (m_Input->IsKeyDown(KeyCode::W)) forward += 1.0f;
            if (m_Input->IsKeyDown(KeyCode::S)) forward -= 1.0f;
            if (m_Input->IsKeyDown(KeyCode::D)) right += 1.0f;
            if (m_Input->IsKeyDown(KeyCode::A)) right -= 1.0f;
            if (m_Input->IsKeyDown(KeyCode::E)) up += 1.0f;
            if (m_Input->IsKeyDown(KeyCode::Q)) up -= 1.0f;
            if (forward != 0.0f || right != 0.0f || up != 0.0f)
            {
                m_Camera.ProcessKeyboard(forward, right, up, deltaTime);
            }

            const float mouseX = m_Input->MouseX();
            const float mouseY = m_Input->MouseY();
            if (m_Input->IsMouseButtonDown(MouseButton::Right))
            {
                if (m_HasLastMouse)
                {
                    const float dx = mouseX - m_LastMouseX;
                    const float dy = m_LastMouseY - mouseY;
                    m_Camera.ProcessMouse(dx, dy);
                }
                m_HasLastMouse = true;
            }
            else
            {
                m_HasLastMouse = false;
            }
            m_LastMouseX = mouseX;
            m_LastMouseY = mouseY;
        }

        void UploadParams(vk::Extent3D extent)
        {
            RaytraceParamsGpu params{};
            params.CameraPos = m_Camera.GetPosition();
            params.CameraFront = m_Camera.GetFront();
            params.CameraUp = m_Camera.GetUp();
            params.CameraRight = m_Camera.GetRight();
            params.Fov = m_Camera.GetFov();
            params.Aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
            params.Time = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_StartTime).count();
            if (m_Scene) FillSceneData(params, *m_Scene);
            m_ParamsBuffer->Upload(&params, sizeof(params));
        }

        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<EventDispatcher> m_Dispatcher;
        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<InputState> m_Input;

        GPP::Scene* m_Scene{nullptr};
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
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(ViewportLayer)
