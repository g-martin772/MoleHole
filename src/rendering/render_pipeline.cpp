module MoleHole;

import :Rendering.Pipeline;
import :Rendering.Data;
import :Rendering.DebugLegend;
import :Rendering.MeshTrace;
import :Rendering.Lut;
import :Simulation.Components;
import :Rendering.OrbitOverlays;
import std;
import vulkan;
import glm;
import GPP;

using namespace GPP;

namespace MoleHole
{
    namespace
    {
        constexpr float kGravityGridPlaneY = -5.0f;
        constexpr float kGravityGridPlaneSize = 200.0f;
        constexpr int kGravityGridResolution = 256;
        constexpr float kGravityGridCellSize = 2.0f;
        constexpr float kGravityGridLineThickness = 0.03f;

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

        // The shared PBR includes live in gpp; pipelines resolve quoted includes against these directories too
        ShaderCompileOptions ShaderIncludeOptions(const RenderResources& res)
        {
            ShaderCompileOptions options;
            auto pbrDirectory = res.FileSystem->ResolvePath("dependencies/gpp/shaders/pbr");
            if (std::error_code error; !std::filesystem::exists(pbrDirectory, error))
            {
                pbrDirectory = res.FileSystem->ResourceDir() / "shaders" / "pbr";
            }
            options.includeDirectories.push_back(std::move(pbrDirectory));
            return options;
        }

        std::shared_ptr<ShaderPipeline> MakeComputePipeline(const RenderResources& res, std::string fileName,
                                                            std::unordered_map<std::string, std::string> defines = {})
        {
            auto pipeline = std::make_shared<ShaderPipeline>(
                res.Device,
                VulkanPipelineSpecification{},
                ShaderPipelineDescription{
                    .compute = ShaderSource{
                        .path = res.FileSystem->ResolveAssetPath("shaders", fileName),
                        .stage = ShaderStage::Compute,
                        .defines = std::move(defines)
                    },
                    .compileOptions = ShaderIncludeOptions(res),
                    .enableHotReload = true
                },
                res.FileSystem, res.Dispatcher, res.Logger);
            if (!pipeline->StartOnRenderThread())
            {
                res.Logger->Error("RenderPipeline: failed to compile compute shader '{}'", fileName);
            }
            return pipeline;
        }

        std::shared_ptr<ShaderPipeline> MakeRasterPipeline(const RenderResources& res, vk::Format colorFormat,
                                                           vk::Format depthFormat, bool blending,
                                                           vk::PrimitiveTopology topology, std::string vertex,
                                                           std::string fragment, const char* what,
                                                           std::unordered_map<std::string, std::string> defines = {})
        {
            auto pipeline = std::make_shared<ShaderPipeline>(
                res.Device,
                VulkanPipelineSpecification{
                    .colorFormat = colorFormat,
                    .depthFormat = depthFormat,
                    .enableBlending = blending,
                    .cullMode = vk::CullModeFlagBits::eNone,
                    .topology = topology
                },
                ShaderPipelineDescription{
                    .vertex = ShaderSource{
                        .path = res.FileSystem->ResolveAssetPath("shaders", vertex),
                        .stage = ShaderStage::Vertex
                    },
                    .fragment = ShaderSource{
                        .path = res.FileSystem->ResolveAssetPath("shaders", fragment),
                        .stage = ShaderStage::Fragment,
                        .defines = std::move(defines)
                    },
                    .compileOptions = ShaderIncludeOptions(res),
                    .enableHotReload = true
                },
                res.FileSystem, res.Dispatcher, res.Logger);
            if (!pipeline->StartOnRenderThread())
            {
                res.Logger->Error("RenderPipeline: failed to compile {} shader", what);
            }
            return pipeline;
        }

        bool Compiled(const std::shared_ptr<ShaderPipeline>& pipeline)
        {
            return pipeline && pipeline->GetPipeline();
        }

        std::optional<std::string> CompileFailure(std::string_view stage,
                                                  std::initializer_list<const std::shared_ptr<ShaderPipeline>*> pipelines)
        {
            for (const auto* pipeline : pipelines)
            {
                if (!*pipeline || (*pipeline)->GetPipeline()) continue;
                if ((*pipeline)->GetCompilationProgress().state == ShaderCompilationState::Failed)
                    return std::format("{} shader failed to compile: {}", stage, (*pipeline)->LastError());
            }
            return std::nullopt;
        }

        void CreateOrResize(RenderResources& res, std::unique_ptr<VulkanImage>& image, vk::Extent3D extent,
                            std::string debugName)
        {
            if (!image)
            {
                image = std::make_unique<VulkanImage>(res.Device, MakeComputeTargetSpec(extent, std::move(debugName)),
                                                      res.Logger);
            }
            else
            {
                image->Resize(extent);
            }
            ImmediateSubmit(*res.UploadPool, res.Device->GetBackgroundQueue(), [&](const vk::CommandBuffer cmd)
            {
                TransitionImageLayout(cmd, image->GetImage(), vk::Format::eR32G32B32A32Sfloat,
                                      vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
            });
        }

        struct DebugLines
        {
            std::shared_ptr<ShaderPipeline> Pipeline;
            vk::Format ColorFormat{vk::Format::eUndefined};
            std::deque<std::shared_ptr<VulkanBuffer>> Retired;
            static constexpr std::size_t kMaxRetired = 3;

            void EnsurePipeline(const RenderResources& res, vk::Format colorFormat)
            {
                if (Pipeline && ColorFormat == colorFormat) return;
                Pipeline = MakeRasterPipeline(res, colorFormat, vk::Format::eUndefined, false,
                                              vk::PrimitiveTopology::eLineList, "physics_debug_line.vert",
                                              "physics_debug_line.frag", "debug line");
                ColorFormat = colorFormat;
            }

            void Retire(std::unique_ptr<VulkanBuffer> oldBuffer)
            {
                if (!oldBuffer) return;
                Retired.push_back(std::shared_ptr<VulkanBuffer>(std::move(oldBuffer)));
                while (Retired.size() > kMaxRetired) Retired.pop_front();
            }

            void Upload(const RenderResources& res, std::unique_ptr<VulkanBuffer>& buffer,
                        const std::vector<float>& vertices)
            {
                const auto byteSize = static_cast<vk::DeviceSize>(vertices.size() * sizeof(float));
                if (!buffer || buffer->GetSize() < byteSize)
                {
                    Retire(std::move(buffer));
                    buffer = std::make_unique<VulkanBuffer>(res.Device, MakeVertexBufferSpecification(byteSize, true),
                                                            res.Logger);
                }
                buffer->Upload(vertices.data(), byteSize);
            }
        };

        // The camera's motion when it is not the ZAMO and the tracer renders it (Integrated transport); rasterised
        // overlays use it to stay on the aberrated image (AberrateForCamera).
        std::optional<CameraObserverFrame> MovingCameraObserver(const RenderFrameContext& ctx)
        {
            if (ctx.Settings.TransportMode != Transport::Integrated) return std::nullopt;
            return FindCameraObserver(ctx.Scene, static_cast<std::int32_t>(ctx.Settings.Solution),
                                      static_cast<ObserverKind>(ctx.Settings.Observer),
                                      static_cast<OrbitDirection>(ctx.Settings.ObserverOrbit), ctx.ViewCamera.GetPosition(),
                                      ctx.Inputs.ObserverGuid);
        }

        void RecordLinePass(RenderFrameContext& ctx, std::string name, const DebugLines& lines,
                            const VulkanBuffer* buffer, std::uint32_t vertexCount)
        {
            const glm::mat4 viewProjection = ctx.ViewCamera.GetViewProjectionMatrix(ctx.Frame);
            ctx.Graph.AddGraphicsPass(
                std::move(name), {}, {},
                {RenderGraphAttachment{.Handle = ctx.ColorTarget, .LoadOp = vk::AttachmentLoadOp::eLoad}},
                std::nullopt,
                [pipelineHolder = lines.Pipeline, buffer, vertexCount, viewProjection](const vk::CommandBuffer cmd,
                                                                                       RenderGraph&)
                {
                    const auto pipeline = pipelineHolder->GetPipeline();
                    if (!pipeline) return;
                    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->GetPipeline());
                    cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4),
                                      &viewProjection);
                    BindVertexBuffer(cmd, buffer->GetBuffer());
                    cmd.draw(vertexCount, 1, 0, 0);
                });
        }

        bool MeshTraceSupported(const GpuCapabilities& caps)
        {
            return caps.RayQuery && caps.AccelerationStructure && caps.DescriptorIndexing;
        }

        struct TracedMeshBindings
        {
            bool Active = false;
            vk::AccelerationStructureKHR Tlas{};
            vk::Buffer Instances{};
            vk::Buffer Materials{};
            vk::Buffer Lights{};
            vk::Buffer LightParams{};
            vk::Buffer Dfg{};
            std::shared_ptr<const std::vector<vk::DescriptorImageInfo>> Textures;
        };

        // Defines of the PBR material library for a pass: texture table size, descriptor indexing, shadow rays
        std::unordered_map<std::string, std::string> MeshShaderDefines(const GpuCapabilities& caps, const std::uint32_t textureCapacity,
                                                                         const bool shadows)
        {
            auto defines = PbrShaderDefines(caps, textureCapacity);
            if (shadows) defines.emplace("MESH_SHADOWS", "1");
            return defines;
        }

        class TraceStage final : public IRenderStage
        {
        public:
            TraceStage(RenderResources& res, const std::chrono::steady_clock::time_point& start)
                : m_Res(res), m_StartTime(start)
            {
            }

            void Initialize()
            {
                std::unordered_map<std::string, std::string> defines;
                m_Pipeline = MakeComputePipeline(m_Res, "black_hole_rendering.comp", defines);
                m_FlatPipeline = MakeComputePipeline(m_Res, "flat_rendering.comp", defines);
                if (m_Res.Device->SupportsShaderFloat64())
                {
                    defines["GPP_PRECISION_F64"] = "1";
                    m_PipelineF64 = MakeComputePipeline(m_Res, "black_hole_rendering.comp", defines);
                }
                if (MeshTraceSupported(m_Res.Device->GetCapabilities()))
                {
                    auto meshDefines = MeshShaderDefines(m_Res.Device->GetCapabilities(),
                                                         GltfTextureCapacity(m_Res.Device->GetCapabilities()), false);
                    meshDefines.emplace("GPP_MESH_TRACE", "1");
                    m_MeshPipeline = MakeComputePipeline(m_Res, "black_hole_rendering.comp", meshDefines);
                    if (m_PipelineF64)
                    {
                        meshDefines.emplace("GPP_PRECISION_F64", "1");
                        m_MeshPipelineF64 = MakeComputePipeline(m_Res, "black_hole_rendering.comp", meshDefines);
                    }
                }
                m_ParamsBuffer = std::make_unique<VulkanBuffer>(
                    m_Res.Device, MakeUniformBufferSpecification(sizeof(RaytraceParamsGpu)), m_Res.Logger);
                const auto storage = [&](const std::size_t bytes)
                {
                    return std::make_unique<VulkanBuffer>(m_Res.Device, MakeStorageBufferSpecification(bytes, true), m_Res.Logger);
                };
                m_TrailSegments = storage(kMaxTracedTrailSegments * sizeof(TracedTrailSegment));
                m_TrailCells = storage(kMaxTrailCells * sizeof(TrailCell));
                m_TrailIndices = storage(kMaxTrailGridEntries * sizeof(std::uint32_t));
                m_TrailCoarse = storage(kTrailCoarseWords * sizeof(std::uint32_t));
            }

            [[nodiscard]] std::string_view Name() const override { return "Trace"; }

            [[nodiscard]] bool Ready() const override
            {
                return Compiled(m_Pipeline) && Compiled(m_FlatPipeline) && (!m_PipelineF64 || Compiled(m_PipelineF64))
                       && MeshPipelinesReady();
            }

            [[nodiscard]] std::optional<std::string> Failure() const override
            {
                return CompileFailure(Name(), {&m_Pipeline, &m_FlatPipeline, &m_PipelineF64, &m_MeshPipeline, &m_MeshPipelineF64});
            }

            void Record(RenderFrameContext& ctx) override
            {
                if (m_MeshPipeline && WantsTracedMeshes(ctx.Settings) && MeshPipelinesReady())
                {
                    auto frame = m_Res.Mesh->Prepare(ctx.Graph, ctx.Scene, ctx.Frame, ctx.Settings, true, *m_Res.DummyWhiteTexture);
                    if (frame.HasTlas)
                    {
                        m_Res.MeshTrace.Active = true;
                        m_Res.MeshTrace.Scene = std::move(frame);
                    }
                }
                UploadParams(ctx);

                const auto kernel = SelectTraceKernel(ctx.Settings, Compiled(m_PipelineF64), m_Res.MeshTrace.Active);
                const std::shared_ptr<ShaderPipeline> raytracePipeline = kernel == TraceKernel::Flat ? m_FlatPipeline
                                                                         : kernel == TraceKernel::MarcherF64 ? m_PipelineF64
                                                                         : kernel == TraceKernel::MarcherMeshes ? m_MeshPipeline
                                                                         : kernel == TraceKernel::MarcherMeshesF64 ? m_MeshPipelineF64
                                                                         : m_Pipeline;
                const std::uint32_t groupsX = ctx.GroupsX;
                const auto device = m_Res.Device;
                auto* res = &m_Res;
                auto* params = m_ParamsBuffer.get();
                const auto& traced = m_Res.MeshTrace;
                TracedMeshBindings bindings{.Active = traced.Active, .Tlas = traced.Scene.Tlas,
                                            .Instances = traced.Scene.Instances, .Materials = traced.Scene.Materials,
                                            .Lights = traced.Scene.Lights, .LightParams = traced.Scene.LightParams, .Dfg = traced.Scene.Dfg,
                                            .Textures = traced.Scene.Textures};
                const std::array<VulkanBuffer*, 4> trailBuffers{m_TrailSegments.get(), m_TrailCells.get(), m_TrailIndices.get(),
                                                                m_TrailCoarse.get()};

                ctx.Graph.AddBudgetedComputePass(
                    "Trace", {}, {}, ctx.GroupsX * ctx.GroupsY,
                    [device, groupsX, raytracePipeline, res, params, trailBuffers, meshes = std::move(bindings), set = vk::DescriptorSet{}](
                        const vk::CommandBuffer cmd, RenderGraph& g, const RenderGraphWorkSlice& slice) mutable
                    {
                        const auto pipeline = raytracePipeline->GetPipeline();
                        if (!pipeline) return;
                        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->GetPipeline());
                        if (!set)
                        {
                            set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                            DescriptorSetWriter writer(device->GetDevice());
                            writer.WriteStorageImage(set, 0, res->RaytraceImage->GetImageView())
                                .WriteUniformBuffer(set, 1, params->GetBuffer());
                            const auto bindings = raytracePipeline->GetReflection().descriptorBindings;
                            const std::array<const VulkanImage*, 4> samplers{
                                res->Skybox.get(), res->BlackbodyLut.get(), nullptr, res->HrDiagramLut.get()};
                            for (std::uint32_t binding = 2; binding < 6; ++binding)
                            {
                                if (std::ranges::none_of(bindings, [binding](const auto& b) { return b.set == 0 && b.binding == binding; }))
                                    continue;
                                const auto* image = samplers[binding - 2];
                                writer.WriteCombinedImageSampler(set, binding, image->GetImageView(), image->GetSampler());
                            }
                            const auto has = [&bindings](const std::uint32_t binding)
                            {
                                return std::ranges::any_of(bindings, [binding](const auto& b) { return b.set == 0 && b.binding == binding; });
                            };
                            for (std::uint32_t binding = 13; binding < 17; ++binding)
                            {
                                if (has(binding)) writer.WriteStorageBuffer(set, binding, trailBuffers[binding - 13]->GetBuffer());
                            }
                            if (meshes.Active)
                            {
                                if (has(6)) writer.WriteAccelerationStructure(set, 6, meshes.Tlas);
                                if (has(7)) writer.WriteStorageBuffer(set, 7, meshes.Instances);
                                if (has(8)) writer.WriteCombinedImageSamplers(set, 8, *meshes.Textures);
                                if (has(9)) writer.WriteStorageBuffer(set, 9, meshes.Materials);
                                if (has(10)) writer.WriteStorageBuffer(set, 10, meshes.Lights);
                                if (has(11)) writer.WriteUniformBuffer(set, 11, meshes.LightParams);
                                if (has(12)) writer.WriteStorageBuffer(set, 12, meshes.Dfg);
                            }
                            writer.Update();
                        }
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->GetLayout(), 0, set, {});
                        const std::array<std::uint32_t, 2> push{slice.First, groupsX};
                        cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0,
                                          sizeof(push), push.data());
                        cmd.dispatch(slice.Count, 1, 1);
                    });
            }

        private:
            void UploadParams(const RenderFrameContext& ctx)
            {
                const auto& camera = ctx.ViewCamera;
                const auto& render = ctx.Settings;

                RaytraceParamsGpu params{};
                params.CameraPos = ctx.Frame.ToRender(camera.GetPosition());
                params.CameraFront = camera.GetFront();
                params.CameraUp = camera.GetUp();
                params.CameraRight = camera.GetRight();
                params.Fov = camera.GetFov();
                params.Aspect = static_cast<float>(ctx.TraceExtent.width) / static_cast<float>(ctx.TraceExtent.height);
                params.ViewProjection = camera.GetViewProjectionMatrix(ctx.Frame);
                FillSceneData(params, ctx.Scene, ctx.Frame);

                params.DebugMode = static_cast<std::int32_t>(render.Debug);
                params.MetricType = static_cast<std::int32_t>(render.Solution);
                params.DiskAccretionRate = render.DiskAccretionRate;
                params.DiskOpacity = render.DiskOpacity;
                params.DiskDisplayTemperature = render.DiskDisplayTemperature;
                params.DiskHotSpots = render.DiskHotSpots;
                FillMetricData(params, m_MetricCache);
                FillDiskPhases(params, ctx.Scene.Clock().Time, render.AccDiskSpeed);
                if (!FillObserverFromBody(params, ctx.Scene, ctx.Inputs.ObserverGuid))
                {
                    FillObserverData(params, static_cast<ObserverKind>(render.Observer),
                                     static_cast<OrbitDirection>(render.ObserverOrbit));
                }
                FillOrbitOverlayData(params, render.ShowCharacteristicRadii && render.TransportMode == Transport::Integrated);
                params.IsPhysicallyAccurate = render.TransportMode == Transport::Integrated ? 1 : 0;
                params.GravitationalLensingEnabled = render.Lensing() ? 1 : 0;
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
                params.EnableThirdPerson = render.ThirdPerson ? 1 : 0;
                params.ThirdPersonDistance = render.ThirdPersonDistance;
                params.ThirdPersonHeight = render.ThirdPersonHeight;
                if (m_Res.MeshTrace.Active)
                {
                    params.MeshBounds = m_Res.MeshTrace.Scene.Bounds;
                    params.NumMeshInstances = m_Res.MeshTrace.Scene.InstanceCount;
                }

                // Lensed trails prepared by OverlayGeometryStage
                const auto& trails = m_Res.Trails;
                if (m_Res.TrailsActive && params.IsPhysicallyAccurate == 1)
                {
                    m_TrailSegments->Upload(trails.Segments.data(), trails.Segments.size() * sizeof(TracedTrailSegment));
                    m_TrailCells->Upload(trails.Cells.data(), trails.Cells.size() * sizeof(TrailCell));
                    m_TrailCoarse->Upload(trails.Coarse.data(), trails.Coarse.size() * sizeof(std::uint32_t));
                    if (!trails.CellSegments.empty())
                    {
                        m_TrailIndices->Upload(trails.CellSegments.data(), trails.CellSegments.size() * sizeof(std::uint32_t));
                    }
                    params.TrailGrid = glm::vec4(trails.Radius, static_cast<float>(kTrailGridCells), 1.0f, m_Res.TrailAngularHalfWidth);
                    params.TrailGrid2 = glm::vec4(static_cast<float>(trails.Cells.size()), 0.0f, 0.0f, 0.0f);
                }

                m_ParamsBuffer->Upload(&params, sizeof(params));
            }

            [[nodiscard]] bool MeshPipelinesReady() const
            {
                return (!m_MeshPipeline || Compiled(m_MeshPipeline)) && (!m_MeshPipelineF64 || Compiled(m_MeshPipelineF64));
            }

            RenderResources& m_Res;
            std::chrono::steady_clock::time_point m_StartTime;
            std::shared_ptr<ShaderPipeline> m_Pipeline;
            std::shared_ptr<ShaderPipeline> m_PipelineF64;
            std::shared_ptr<ShaderPipeline> m_FlatPipeline;
            std::shared_ptr<ShaderPipeline> m_MeshPipeline;
            std::shared_ptr<ShaderPipeline> m_MeshPipelineF64;
            std::unique_ptr<VulkanBuffer> m_ParamsBuffer;
            MetricCache m_MetricCache;
            std::unique_ptr<VulkanBuffer> m_TrailSegments;
            std::unique_ptr<VulkanBuffer> m_TrailCells;
            std::unique_ptr<VulkanBuffer> m_TrailIndices;
            std::unique_ptr<VulkanBuffer> m_TrailCoarse;
        };

        // Meters the traced image and writes the tone mapper's exposure to a 1x1 image (always 1 without tone mapping)
        class ExposureStage final : public IRenderStage
        {
        public:
            explicit ExposureStage(RenderResources& res) : m_Res(res)
            {
                m_Pipeline = MakeComputePipeline(res, "exposure.comp");
            }

            [[nodiscard]] std::string_view Name() const override { return "Exposure"; }
            [[nodiscard]] bool Ready() const override { return Compiled(m_Pipeline); }
            [[nodiscard]] std::optional<std::string> Failure() const override { return CompileFailure(Name(), {&m_Pipeline}); }

            void Record(RenderFrameContext& ctx) override
            {
                const auto device = m_Res.Device;
                auto* res = &m_Res;
                const auto exposure = m_Pipeline;
                const bool physical = ctx.Settings.Exposure == ExposureMode::Physical;
                // Image value 1 is the 203 cd/m^2 reference white: the photometric exposure scale acts on cd/m^2
                const float physicalScale = physical
                    ? static_cast<float>(Radiometry::ExposureScale(Radiometry::PhysicalCamera{
                          ctx.Settings.CameraAperture, ctx.Settings.CameraShutter, ctx.Settings.CameraIso})
                                         * Radiometry::kHdrReferenceWhite)
                    : 0.0f;
                struct { std::int32_t Enabled; std::int32_t Auto; float Ev; float Key; float PhysicalScale; } push{
                    ctx.Settings.ToneMapping != ToneMap::None ? 1 : 0,
                    ctx.Settings.Exposure == ExposureMode::Auto ? 1 : 0,
                    ctx.Settings.ExposureEv, ctx.Settings.AutoExposureKey, physicalScale};
                ctx.Graph.AddComputePass(
                    "Exposure", {}, {},
                    [device, res, exposure, push](const vk::CommandBuffer cmd, RenderGraph& g)
                    {
                        const auto pipeline = exposure->GetPipeline();
                        if (!pipeline) return;
                        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->GetPipeline());
                        const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                        DescriptorSetWriter(device->GetDevice())
                            .WriteStorageImage(set, 0, res->RaytraceImage->GetImageView())
                            .WriteStorageImage(set, 1, res->ExposureImage->GetImageView())
                            .Update();
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->GetLayout(), 0, set, {});
                        cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push);
                        cmd.dispatch(1, 1, 1);
                    });
            }

        private:
            RenderResources& m_Res;
            std::shared_ptr<ShaderPipeline> m_Pipeline;
        };

        class BloomStage final : public IRenderStage
        {
        public:
            explicit BloomStage(RenderResources& res) : m_Res(res)
            {
                m_ExtractPipeline = MakeComputePipeline(res, "bloom_extract.comp");
                m_BlurPipeline = MakeComputePipeline(res, "bloom_blur.comp");
            }

            [[nodiscard]] std::string_view Name() const override { return "Bloom"; }
            [[nodiscard]] bool Enabled(const RenderSettings& settings) const override { return settings.UsesBloomChain(); }
            [[nodiscard]] bool Ready() const override { return Compiled(m_ExtractPipeline) && Compiled(m_BlurPipeline); }
            [[nodiscard]] std::optional<std::string> Failure() const override
            {
                return CompileFailure(Name(), {&m_ExtractPipeline, &m_BlurPipeline});
            }

            void Record(RenderFrameContext& ctx) override
            {
                const auto device = m_Res.Device;
                const auto groupsX = ctx.GroupsX;
                const auto groupsY = ctx.GroupsY;
                auto* res = &m_Res;
                const auto extract = m_ExtractPipeline;
                const auto blur = m_BlurPipeline;

                ctx.Graph.AddComputePass(
                    "BloomExtract", {}, {},
                    [device, groupsX, groupsY, res, extract, threshold = ctx.Settings.BloomThreshold](
                        const vk::CommandBuffer cmd, RenderGraph& g)
                    {
                        const auto pipeline = extract->GetPipeline();
                        if (!pipeline) return;
                        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->GetPipeline());
                        const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                        DescriptorSetWriter(device->GetDevice())
                            .WriteStorageImage(set, 0, res->RaytraceImage->GetImageView())
                            .WriteStorageImage(set, 1, res->BloomBrightImage->GetImageView())
                            .WriteStorageImage(set, 2, res->ExposureImage->GetImageView())
                            .Update();
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->GetLayout(), 0, set, {});
                        cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(float), &threshold);
                        cmd.dispatch(groupsX, groupsY, 1);
                    });

                // The blur kernel is fixed in tracer pixels; a Gaussian's width grows with the square root of the
                // pass count, so passes follow the square of the scale to keep the glow the same size on screen
                const float scale = std::clamp(ctx.Settings.ResolutionScale, 25.0f, 200.0f) / 100.0f;
                const int passes = std::clamp(static_cast<int>(std::lround(static_cast<float>(ctx.Settings.BloomBlurPasses) * scale * scale)), 1, 20);
                int finalBlurIndex = 0;
                VulkanImage* src = m_Res.BloomBrightImage.get();
                for (int i = 0; i < passes * 2; ++i)
                {
                    const int horizontal = (i % 2 == 0) ? 1 : 0;
                    const int dstIndex = horizontal ? 0 : 1;
                    VulkanImage* dst = m_Res.BloomBlurImages[static_cast<std::size_t>(dstIndex)].get();
                    ctx.Graph.AddComputePass(
                        "BloomBlur", {}, {},
                        [device, groupsX, groupsY, src, dst, horizontal, blur](const vk::CommandBuffer cmd, RenderGraph& g)
                        {
                            const auto pipeline = blur->GetPipeline();
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
                m_Res.BloomResult = m_Res.BloomBlurImages[static_cast<std::size_t>(finalBlurIndex)].get();
            }

        private:
            RenderResources& m_Res;
            std::shared_ptr<ShaderPipeline> m_ExtractPipeline;
            std::shared_ptr<ShaderPipeline> m_BlurPipeline;
        };

        class LensFlareStage final : public IRenderStage
        {
        public:
            explicit LensFlareStage(RenderResources& res) : m_Res(res)
            {
                m_Pipeline = MakeComputePipeline(res, "lens_flare.comp");
            }

            [[nodiscard]] std::string_view Name() const override { return "LensFlare"; }
            [[nodiscard]] bool Enabled(const RenderSettings& settings) const override { return settings.LensFlare; }
            [[nodiscard]] bool Ready() const override { return Compiled(m_Pipeline); }
            [[nodiscard]] std::optional<std::string> Failure() const override { return CompileFailure(Name(), {&m_Pipeline}); }

            void Record(RenderFrameContext& ctx) override
            {
                const auto device = m_Res.Device;
                auto* res = &m_Res;
                const auto flare = m_Pipeline;
                const auto groupsX = ctx.GroupsX;
                const auto groupsY = ctx.GroupsY;
                VulkanImage* blurred = m_Res.BloomResult;
                ctx.Graph.AddComputePass(
                    "LensFlare", {}, {},
                    [device, groupsX, groupsY, res, flare, blurred, settings = ctx.Settings](
                        const vk::CommandBuffer cmd, RenderGraph& g)
                    {
                        const auto pipeline = flare->GetPipeline();
                        if (!pipeline) return;
                        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->GetPipeline());
                        const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                        DescriptorSetWriter(device->GetDevice())
                            .WriteStorageImage(set, 0, blurred->GetImageView())
                            .WriteStorageImage(set, 1, res->LensFlareImage->GetImageView())
                            .WriteStorageImage(set, 2, res->ExposureImage->GetImageView())
                            .Update();
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->GetLayout(), 0, set, {});
                        struct { float Intensity; float Threshold; std::int32_t Enabled; } push{
                            settings.LensFlareIntensity, settings.LensFlareThreshold, settings.LensFlare ? 1 : 0
                        };
                        cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push);
                        cmd.dispatch(groupsX, groupsY, 1);
                    });
            }

        private:
            RenderResources& m_Res;
            std::shared_ptr<ShaderPipeline> m_Pipeline;
        };

        class CompositeStage final : public IRenderStage
        {
        public:
            explicit CompositeStage(RenderResources& res) : m_Res(res) {}

            [[nodiscard]] std::string_view Name() const override { return "Composite"; }
            [[nodiscard]] bool Ready() const override { return Compiled(m_Pipeline); }
            [[nodiscard]] std::optional<std::string> Failure() const override { return CompileFailure(Name(), {&m_Pipeline}); }

            void Record(RenderFrameContext& ctx) override
            {
                if (!m_Pipeline || m_ColorFormat != ctx.ColorFormat || m_DepthFormat != ctx.DepthFormat)
                {
                    m_Pipeline = MakeRasterPipeline(m_Res, ctx.ColorFormat, ctx.DepthFormat, false,
                                                    vk::PrimitiveTopology::eTriangleList, "blackhole_display.vert",
                                                    "blackhole_display.frag", "composite display");
                    m_ColorFormat = ctx.ColorFormat;
                    m_DepthFormat = ctx.DepthFormat;
                }
                if (!m_Pipeline) return;

                std::optional<RenderGraphAttachment> depthAttachment;
                if (ctx.DepthTarget != kInvalidRenderGraphHandle)
                {
                    depthAttachment = RenderGraphAttachment{
                        .Handle = ctx.DepthTarget, .LoadOp = vk::AttachmentLoadOp::eClear,
                        .Clear = vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0))
                    };
                }
                const auto device = m_Res.Device;
                auto* res = &m_Res;
                const auto composite = m_Pipeline;
                VulkanImage* bloom = m_Res.BloomResult;
                const auto traceExtent = ctx.TraceExtent;
                ctx.Graph.AddGraphicsPass(
                    "Composite", {}, {},
                    {RenderGraphAttachment{
                        .Handle = ctx.ColorTarget, .LoadOp = vk::AttachmentLoadOp::eClear,
                        .Clear = vk::ClearValue(vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f))
                    }},
                    depthAttachment,
                    [device, res, composite, bloom, traceExtent, settings = ctx.Settings](
                        const vk::CommandBuffer cmd, RenderGraph& g)
                    {
                        const auto pipeline = composite->GetPipeline();
                        if (!pipeline) return;
                        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->GetPipeline());
                        const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                        DescriptorSetWriter(device->GetDevice())
                            .WriteCombinedImageSampler(set, 0, res->RaytraceImage->GetImageView(), res->RaytraceImage->GetSampler(),
                                                       vk::ImageLayout::eGeneral)
                            .WriteCombinedImageSampler(set, 1, bloom->GetImageView(), bloom->GetSampler(),
                                                       vk::ImageLayout::eGeneral)
                            .WriteCombinedImageSampler(set, 2, res->LensFlareImage->GetImageView(), res->LensFlareImage->GetSampler(),
                                                       vk::ImageLayout::eGeneral)
                            .WriteCombinedImageSampler(set, 3, res->ExposureImage->GetImageView(), res->ExposureImage->GetSampler(),
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
                            std::int32_t ToneMapping;
                        } push{settings.Fxaa ? 1 : 0, settings.Bloom ? 1 : 0, settings.BloomIntensity,
                               settings.BloomDebug ? 1 : 0, settings.LensFlare ? 1 : 0, settings.LensFlareIntensity,
                               static_cast<float>(traceExtent.width), static_cast<float>(traceExtent.height),
                               static_cast<std::int32_t>(settings.ToneMapping)};
                        cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eFragment, 0, sizeof(push), &push);
                        cmd.draw(3, 1, 0, 0);
                    });
            }

        private:
            RenderResources& m_Res;
            std::shared_ptr<ShaderPipeline> m_Pipeline;
            vk::Format m_ColorFormat{vk::Format::eUndefined};
            vk::Format m_DepthFormat{vk::Format::eUndefined};
        };

        class GravityGridStage final : public IRenderStage
        {
        public:
            explicit GravityGridStage(RenderResources& res) : m_Res(res)
            {
                m_ParamsBuffer = std::make_unique<VulkanBuffer>(
                    res.Device, MakeUniformBufferSpecification(sizeof(GravityGridParamsGpu)), res.Logger);
                CreateMesh();
            }

            [[nodiscard]] std::string_view Name() const override { return "GravityGrid"; }
            [[nodiscard]] bool Enabled(const RenderSettings& settings) const override { return settings.ShowGravityGrid; }

            void Record(RenderFrameContext& ctx) override
            {
                if (!m_Pipeline || m_ColorFormat != ctx.ColorFormat)
                {
                    m_Pipeline = MakeRasterPipeline(m_Res, ctx.ColorFormat, vk::Format::eUndefined, true,
                                                    vk::PrimitiveTopology::eTriangleList, "plane_grid.vert",
                                                    "plane_grid.frag", "gravity grid");
                    m_ColorFormat = ctx.ColorFormat;
                }
                if (!m_Pipeline) return;

                GravityGridParamsGpu params{};
                params.ViewProjection = ctx.ViewCamera.GetViewProjectionMatrix(ctx.Frame)
                                        * glm::translate(glm::mat4(1.0f), ctx.Frame.ToRender(glm::dvec3(0.0)));
                params.PlaneY = kGravityGridPlaneY;
                params.CellSize = kGravityGridCellSize;
                params.LineThickness = kGravityGridLineThickness;
                params.Opacity = kGravityGridOpacity;
                params.Color = glm::vec3(kGravityGridColor.R, kGravityGridColor.G, kGravityGridColor.B);
                FillGravityGridData(params, ctx.Scene, ctx.Frame);
                m_ParamsBuffer->Upload(&params, sizeof(params));

                const auto device = m_Res.Device;
                const auto grid = m_Pipeline;
                auto* self = this;
                ctx.Graph.AddGraphicsPass(
                    "GravityGrid", {}, {},
                    {RenderGraphAttachment{.Handle = ctx.ColorTarget, .LoadOp = vk::AttachmentLoadOp::eLoad}},
                    std::nullopt,
                    [device, grid, self](const vk::CommandBuffer cmd, RenderGraph& g)
                    {
                        const auto pipeline = grid->GetPipeline();
                        if (!pipeline || self->m_IndexCount == 0) return;
                        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->GetPipeline());
                        const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                        DescriptorSetWriter(device->GetDevice())
                            .WriteUniformBuffer(set, 0, self->m_ParamsBuffer->GetBuffer())
                            .Update();
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline->GetLayout(), 0, set, {});
                        BindVertexBuffer(cmd, self->m_VertexBuffer->GetBuffer());
                        BindIndexBuffer(cmd, self->m_IndexBuffer->GetBuffer());
                        cmd.drawIndexed(self->m_IndexCount, 1, 0, 0, 0);
                    });
            }

        private:
            void CreateMesh()
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

                const auto vertexBytes = static_cast<vk::DeviceSize>(vertices.size() * sizeof(glm::vec3));
                m_VertexBuffer = std::make_unique<VulkanBuffer>(
                    m_Res.Device, MakeVertexBufferSpecification(vertexBytes, true), m_Res.Logger);
                m_VertexBuffer->Upload(vertices.data(), vertexBytes);

                const auto indexBytes = static_cast<vk::DeviceSize>(indices.size() * sizeof(std::uint32_t));
                m_IndexBuffer = std::make_unique<VulkanBuffer>(
                    m_Res.Device, MakeIndexBufferSpecification(indexBytes, true), m_Res.Logger);
                m_IndexBuffer->Upload(indices.data(), indexBytes);
                m_IndexCount = static_cast<std::uint32_t>(indices.size());
            }

            RenderResources& m_Res;
            std::shared_ptr<ShaderPipeline> m_Pipeline;
            vk::Format m_ColorFormat{vk::Format::eUndefined};
            std::unique_ptr<VulkanBuffer> m_ParamsBuffer;
            std::unique_ptr<VulkanBuffer> m_VertexBuffer;
            std::unique_ptr<VulkanBuffer> m_IndexBuffer;
            std::uint32_t m_IndexCount{0};
        };

        class PhysicsDebugStage final : public IRenderStage
        {
        public:
            PhysicsDebugStage(RenderResources& res, DebugLines& lines) : m_Res(res), m_Lines(lines) {}

            [[nodiscard]] std::string_view Name() const override { return "PhysicsDebug"; }
            // Also runs once when switched off so PhysX stops producing the lines
            [[nodiscard]] bool Enabled(const RenderSettings& settings) const override
            {
                return settings.ShowPhysicsDebug || m_WasEnabled;
            }

            void Record(RenderFrameContext& ctx) override
            {
                const bool enabled = ctx.Settings.ShowPhysicsDebug;
                m_WasEnabled = enabled;
                const auto& physics = ctx.Inputs.Physics;
                if (!physics) return;
                physics->SetDebugVisualizationEnabled(enabled);
                if (!enabled) return;

                const auto lines = physics->GetDebugLines();
                if (lines.empty()) return;

                std::vector<float> vertices;
                vertices.reserve(lines.size() * 2 * 6);
                const glm::dvec3 physicsOrigin = physics->Origin();
                const auto toRender = [&](const physx::PxVec3& p)
                {
                    return ctx.Frame.ToRender(physicsOrigin + glm::dvec3(p.x, p.y, p.z));
                };
                const auto append = [&](const glm::vec3& position, const physx::PxU32 argb)
                {
                    const auto color = PhysicsDebugColor(argb);
                    vertices.insert(vertices.end(), {position.x, position.y, position.z, color.R, color.G, color.B});
                };
                for (const auto& line : lines)
                {
                    append(toRender(line.pos0), line.color0);
                    append(toRender(line.pos1), line.color1);
                }

                m_Lines.EnsurePipeline(m_Res, ctx.ColorFormat);
                if (!m_Lines.Pipeline) return;
                m_Lines.Upload(m_Res, m_Buffer, vertices);
                RecordLinePass(ctx, "PhysicsDebug", m_Lines, m_Buffer.get(),
                               static_cast<std::uint32_t>(lines.size()) * 2);
            }

        private:
            RenderResources& m_Res;
            DebugLines& m_Lines;
            std::unique_ptr<VulkanBuffer> m_Buffer;
            bool m_WasEnabled{false};
        };

        // Every rasterised overlay line of the frame (characteristic radii of non-traced holes, predicted orbits, object
        // path trails), computed once before the trace. With the Integrated transport the parts inside the first hole's
        // integration sphere go to the tracer instead (res.Trails), which draws them lensed; the rest stays rasterised
        // and is aberrated for a moving camera. Predictions are integrated again only when a body's state or the span
        // changed, so a paused scene costs nothing per frame. See docs/notes/orbit_overlays_and_nodes.md.
        class OverlayGeometryStage final : public IRenderStage
        {
        public:
            OverlayGeometryStage(RenderResources& res, ObjectPathTracker& tracker, std::mutex& mutex)
                : m_Res(res), m_Tracker(tracker), m_Mutex(mutex)
            {
            }

            [[nodiscard]] std::string_view Name() const override { return "OverlayGeometry"; }
            [[nodiscard]] bool Enabled(const RenderSettings& settings) const override
            {
                return settings.ShowObjectPaths || settings.ShowCharacteristicRadii || settings.ShowPredictedOrbits;
            }

            void Record(RenderFrameContext& ctx) override
            {
                static const glm::vec3 kMeshColor{0.2f, 0.8f, 0.2f};
                static const glm::vec3 kSphereColor{0.8f, 0.2f, 0.8f};

                const bool integrated = ctx.Settings.TransportMode == Transport::Integrated;
                const auto primary = integrated ? PrimaryHole(ctx.Scene) : std::nullopt;

                std::vector<OverlaySegment> radii;
                if (ctx.Settings.ShowCharacteristicRadii)
                {
                    // The integrated tracer draws the first hole's rings itself (orbitRingColor); other holes stay lines
                    radii = CharacteristicRadiiOverlay(ctx.Scene, 96, primary ? primary->first : 0);
                }
                std::vector<OverlaySegment> trails;
                if (ctx.Settings.ShowPredictedOrbits)
                {
                    UpdatePredictions(ctx.Scene, ctx.Settings.PredictionSpan, ctx.Inputs.ObserverGuid);
                    trails = m_Predictions;
                }
                if (ctx.Settings.ShowObjectPaths)
                {
                    std::scoped_lock lock(m_Mutex);
                    const auto append = [&](const auto& histories, const glm::vec3& color)
                    {
                        for (const auto& [guid, history] : histories)
                        {
                            for (std::size_t i = 1; i < history.size(); ++i) trails.push_back({history[i - 1], history[i], color});
                        }
                    };
                    append(m_Tracker.MeshHistories(), kMeshColor);
                    append(m_Tracker.SphereHistories(), kSphereColor);
                }

                std::vector<RenderSegment> render;
                render.reserve(radii.size() + trails.size());
                for (const auto& segment : radii) render.push_back({ctx.Frame.ToRender(segment.A), ctx.Frame.ToRender(segment.B), segment.Color});
                std::vector<RenderSegment> raster = render;
                render.clear();
                for (const auto& segment : trails) render.push_back({ctx.Frame.ToRender(segment.A), ctx.Frame.ToRender(segment.B), segment.Color});
                if (primary)
                {
                    // Object paths record a point per tick; 1 % of M is far below anything visible
                    const auto& hole = ctx.Scene.Registry().get<const BlackHoleComponent>(ctx.Scene.FindByGuid(primary->first));
                    render = SimplifyTrails(render, 0.01f * ctx.Frame.GeometricMass(hole.Mass));
                }

                if (primary && !render.empty())
                {
                    const auto entity = ctx.Scene.FindByGuid(primary->first);
                    const auto& hole = ctx.Scene.Registry().get<const BlackHoleComponent>(entity);
                    const glm::vec3 axis = EffectiveSpinAxis(hole.SpinAxis, BlackHoleSignedSpin(hole));
                    const glm::vec3 center = ctx.Frame.ToRender(primary->second.Position);
                    const float radius = kTracerIntegrationRadius * ctx.Frame.GeometricMass(hole.Mass);
                    // Line half-width: ~0.9 trace pixels, as an angle
                    const float fov = glm::radians(ctx.ViewCamera.GetFov());
                    const float halfWidth = 0.9f * 2.0f * std::tan(0.5f * fov) / static_cast<float>(std::max(ctx.TraceExtent.height, 1u));
                    const float pad = halfWidth * (glm::length(center) + radius);
                    m_Res.Trails = SplitTrailsForTracer(render, center, axis, radius, pad, raster);
                    m_Res.TrailAngularHalfWidth = halfWidth;
                    m_Res.TrailsActive = !m_Res.Trails.Segments.empty();
                }
                else
                {
                    raster.insert(raster.end(), render.begin(), render.end());
                }

                // A moving camera sees the traced image aberrated; the rasterised lines follow it
                const auto observer = MovingCameraObserver(ctx);
                const glm::dvec3 camera = ctx.ViewCamera.GetPosition();
                const auto place = [&](const glm::vec3& point)
                {
                    return observer ? ctx.Frame.ToRender(AberrateForCamera(*observer, camera, ctx.Frame.ToWorld(point))) : point;
                };
                auto& vertices = m_Res.OverlayVertices;
                vertices.reserve(raster.size() * 12);
                for (const auto& segment : raster)
                {
                    const glm::vec3 a = place(segment.A);
                    const glm::vec3 b = place(segment.B);
                    const glm::vec3 c = segment.Color;
                    vertices.insert(vertices.end(), {a.x, a.y, a.z, c.r, c.g, c.b, b.x, b.y, b.z, c.r, c.g, c.b});
                }
            }

        private:
            void UpdatePredictions(const GPP::Scene& scene, const float span, const std::uint64_t exclude)
            {
                // A re-launched body starts again at λ = 0, so position is part of the key
                std::vector<std::array<double, 4>> key;
                for (auto [entity, body] : scene.Registry().view<const GeodesicBodyComponent>().each())
                {
                    const bool moving = body.Status == GeodesicBodyStatus::FreeFall;
                    key.push_back({static_cast<double>(scene.GuidOf(entity)), moving ? body.State.Lambda : -1.0, body.State.R,
                                   body.State.Phi});
                }
                if (key == m_PredictionKey && span == m_PredictionSpan && exclude == m_PredictionExclude) return;
                m_PredictionKey = std::move(key);
                m_PredictionSpan = span;
                m_PredictionExclude = exclude;
                m_Predictions = PredictedOrbitsOverlay(scene, span, 240, exclude);
            }

            RenderResources& m_Res;
            ObjectPathTracker& m_Tracker;
            std::mutex& m_Mutex;
            std::vector<OverlaySegment> m_Predictions;
            std::vector<std::array<double, 4>> m_PredictionKey;
            float m_PredictionSpan{0.0f};
            std::uint64_t m_PredictionExclude{0};
        };

        // Draws the rasterised overlay lines OverlayGeometryStage prepared.
        class OverlayLineStage final : public IRenderStage
        {
        public:
            OverlayLineStage(RenderResources& res, DebugLines& lines) : m_Res(res), m_Lines(lines) {}

            [[nodiscard]] std::string_view Name() const override { return "OverlayLines"; }
            [[nodiscard]] bool Enabled(const RenderSettings& settings) const override
            {
                return settings.ShowObjectPaths || settings.ShowCharacteristicRadii || settings.ShowPredictedOrbits;
            }

            void Record(RenderFrameContext& ctx) override
            {
                const auto& vertices = m_Res.OverlayVertices;
                if (vertices.empty()) return;
                m_Lines.EnsurePipeline(m_Res, ctx.ColorFormat);
                if (!m_Lines.Pipeline) return;
                m_Lines.Upload(m_Res, m_Buffer, vertices);
                // 6 floats (pos+color) per vertex, 2 vertices per segment
                RecordLinePass(ctx, "OverlayLines", m_Lines, m_Buffer.get(), static_cast<std::uint32_t>(vertices.size() / 6));
            }

        private:
            RenderResources& m_Res;
            DebugLines& m_Lines;
            std::unique_ptr<VulkanBuffer> m_Buffer;
        };

        class MeshStage final : public IRenderStage
        {
        public:
            explicit MeshStage(RenderResources& res) : m_Res(res)
            {
                m_CameraBuffer = std::make_unique<VulkanBuffer>(
                    res.Device, MakeUniformBufferSpecification(sizeof(MeshCameraParamsGpu)), res.Logger);
            }

            [[nodiscard]] std::string_view Name() const override { return "Meshes"; }
            [[nodiscard]] bool Enabled(const RenderSettings& settings) const override
            {
                return settings.RenderMeshes && !m_Res.MeshTrace.Active;
            }

            void Record(RenderFrameContext& ctx) override
            {
                if (ctx.DepthTarget == kInvalidRenderGraphHandle) return;
                const auto& caps = m_Res.Device->GetCapabilities();
                const bool shadows = m_Res.Mesh->ShadowsAvailable();
                if (!m_Pipeline || m_ColorFormat != ctx.ColorFormat || m_DepthFormat != ctx.DepthFormat)
                {
                    m_Pipeline = MakeRasterPipeline(m_Res, ctx.ColorFormat, ctx.DepthFormat, false,
                                                    vk::PrimitiveTopology::eTriangleList, "mesh.vert", "mesh.frag",
                                                    "mesh overlay",
                                                    MeshShaderDefines(caps, GltfTextureCapacity(caps), shadows));
                    m_ColorFormat = ctx.ColorFormat;
                    m_DepthFormat = ctx.DepthFormat;
                }
                auto frame = m_Res.Mesh->Prepare(ctx.Graph, ctx.Scene, ctx.Frame, ctx.Settings, shadows,
                                                 *m_Res.DummyWhiteTexture);
                if (!m_Pipeline || frame.Draws.empty() || (shadows && !frame.HasTlas)) return;

                MeshCameraParamsGpu params{};
                params.View = ctx.ViewCamera.GetViewMatrix(ctx.Frame);
                params.Projection = ctx.ViewCamera.GetProjectionMatrix();
                params.CameraPos = ctx.Frame.ToRender(ctx.ViewCamera.GetPosition());
                params.ToneMapping = static_cast<std::int32_t>(ctx.Settings.ToneMapping);
                m_CameraBuffer->Upload(&params, sizeof(params));

                const auto device = m_Res.Device;
                const auto meshPipeline = m_Pipeline;
                auto* cameraBuffer = m_CameraBuffer.get();
                auto* exposure = m_Res.ExposureImage.get();
                ctx.Graph.AddGraphicsPass(
                    "Meshes", {}, {},
                    {RenderGraphAttachment{.Handle = ctx.ColorTarget, .LoadOp = vk::AttachmentLoadOp::eLoad}},
                    RenderGraphAttachment{.Handle = ctx.DepthTarget, .LoadOp = vk::AttachmentLoadOp::eLoad},
                    [device, meshPipeline, cameraBuffer, exposure, frame = std::move(frame)](
                        const vk::CommandBuffer cmd, RenderGraph& g)
                    {
                        const auto pipeline = meshPipeline->GetPipeline();
                        if (!pipeline) return;
                        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->GetPipeline());

                        const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                        const auto bindings = meshPipeline->GetReflection().descriptorBindings;
                        const auto has = [&bindings](const std::uint32_t binding)
                        {
                            return std::ranges::any_of(bindings, [binding](const auto& b) { return b.set == 0 && b.binding == binding; });
                        };
                        DescriptorSetWriter writer(device->GetDevice());
                        writer.WriteUniformBuffer(set, 0, cameraBuffer->GetBuffer());
                        if (has(1)) writer.WriteStorageBuffer(set, 1, frame.Materials);
                        if (has(2)) writer.WriteCombinedImageSampler(set, 2, exposure->GetImageView(), exposure->GetSampler(),
                                                                     vk::ImageLayout::eGeneral);
                        if (has(3)) writer.WriteCombinedImageSamplers(set, 3, *frame.Textures);
                        if (has(4)) writer.WriteStorageBuffer(set, 4, frame.Lights);
                        if (has(5)) writer.WriteUniformBuffer(set, 5, frame.LightParams);
                        if (has(6)) writer.WriteAccelerationStructure(set, 6, frame.Tlas);
                        if (has(7)) writer.WriteStorageBuffer(set, 7, frame.Dfg);
                        writer.Update();
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline->GetLayout(), 0, set, {});

                        for (const auto& draw : frame.Draws)
                        {
                            MeshPushConstantsGpu push{};
                            push.Model = draw.Model;
                            push.ExtraAddress = draw.Primitive->ExtraBuffer.GetDeviceAddress();
                            push.MaterialIndex = draw.MaterialIndex;
                            cmd.pushConstants(pipeline->GetLayout(),
                                              vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                              0, sizeof(push), &push);

                            BindVertexBuffer(cmd, draw.Primitive->VertexBuffer.GetBuffer());
                            BindIndexBuffer(cmd, draw.Primitive->IndexBuffer.GetBuffer());
                            cmd.drawIndexed(draw.Primitive->IndexCount, 1, 0, 0, 0);
                        }
                    });
            }

        private:
            RenderResources& m_Res;
            std::shared_ptr<ShaderPipeline> m_Pipeline;
            vk::Format m_ColorFormat{vk::Format::eUndefined};
            vk::Format m_DepthFormat{vk::Format::eUndefined};
            std::unique_ptr<VulkanBuffer> m_CameraBuffer;
        };
    }

    struct RenderPipeline::Impl
    {
        RenderResources Res;
        ObjectPathTracker Paths;
        std::mutex PathMutex;
        DebugLines Lines;
        std::chrono::steady_clock::time_point StartTime{std::chrono::steady_clock::now()};
        std::vector<std::unique_ptr<IRenderStage>> Stages;
        mutable std::mutex RecordedMutex;
        std::vector<std::string> Recorded;
        bool Initialized = false;
    };

    RenderPipeline::RenderPipeline(std::shared_ptr<Renderer> renderer, std::shared_ptr<IFileSystem> fileSystem,
                                   std::shared_ptr<EventDispatcher> dispatcher, std::shared_ptr<Logger> logger,
                                   std::shared_ptr<MeshCache> meshes)
        : m_Impl(std::make_unique<Impl>())
    {
        m_Impl->Res.Renderer = std::move(renderer);
        m_Impl->Res.FileSystem = std::move(fileSystem);
        m_Impl->Res.Dispatcher = std::move(dispatcher);
        m_Impl->Res.Logger = std::move(logger);
        m_Impl->Res.Meshes = std::move(meshes);
    }

    RenderPipeline::~RenderPipeline() = default;

    void RenderPipeline::Initialize()
    {
        auto& impl = *m_Impl;
        auto& res = impl.Res;
        res.Device = res.Renderer->GetDevice();
        res.UploadPool = std::make_unique<VulkanCommandPool>(res.Device, res.Logger, res.Device->GetQueueIndices().Graphics);
        const auto queue = res.Device->GetBackgroundQueue();
        res.Mesh = std::make_unique<MeshScene>(res.Device, res.Meshes, res.Logger, *res.UploadPool);

        // Before the trace: it hands the lensed part of the trails to the tracer
        impl.Stages.push_back(std::make_unique<OverlayGeometryStage>(res, impl.Paths, impl.PathMutex));
        auto trace = std::make_unique<TraceStage>(res, impl.StartTime);
        trace->Initialize();
        impl.Stages.push_back(std::move(trace));
        impl.Stages.push_back(std::make_unique<ExposureStage>(res));
        impl.Stages.push_back(std::make_unique<BloomStage>(res));
        impl.Stages.push_back(std::make_unique<LensFlareStage>(res));
        impl.Stages.push_back(std::make_unique<CompositeStage>(res));
        impl.Stages.push_back(std::make_unique<GravityGridStage>(res));
        impl.Stages.push_back(std::make_unique<PhysicsDebugStage>(res, impl.Lines));
        impl.Stages.push_back(std::make_unique<OverlayLineStage>(res, impl.Lines));
        impl.Stages.push_back(std::make_unique<MeshStage>(res));

        res.BlackbodyLut = GenerateBlackbodyLut(res.Device, *res.UploadPool, queue, res.Logger);
        res.HrDiagramLut = GenerateHrDiagramLut(res.Device, *res.UploadPool, queue, res.Logger);
        res.Skybox = LoadSkyboxTexture(res.Device, *res.UploadPool, queue, res.FileSystem,
                                       "assets/backgrounds/space.hdr", res.Logger);
        res.DummyWhiteTexture = GenerateSolidColorTexture(res.Device, *res.UploadPool, queue, glm::vec4(1.0f), res.Logger);
        impl.Initialized = true;
    }

    void RenderPipeline::RecordObjectPaths(const Scene& scene)
    {
        std::scoped_lock lock(m_Impl->PathMutex);
        m_Impl->Paths.RecordPositions(scene);
    }

    void RenderPipeline::ClearObjectPaths()
    {
        std::scoped_lock lock(m_Impl->PathMutex);
        m_Impl->Paths.Clear();
    }

    void RenderPipeline::Build(RenderGraph& graph, const RenderInputs& inputs)
    {
        auto& impl = *m_Impl;
        auto& res = impl.Res;
        const auto colorTarget = graph.GetPrimaryColorTarget();
        if (!impl.Initialized || !inputs.Scene || colorTarget == kInvalidRenderGraphHandle) return;
        const auto extent = graph.GetImageExtent(colorTarget);
        if (extent.width == 0 || extent.height == 0) return;

        Camera camera = inputs.ViewCamera;
        camera.SetAspect(static_cast<float>(extent.width) / static_cast<float>(extent.height));
        const RenderFrame frame = RenderFrame::ForScene(*inputs.Scene, inputs.Settings.LengthUnit, camera.GetPosition());

        const auto depthTarget = graph.GetPrimaryDepthTarget();
        const auto depthFormat = depthTarget != kInvalidRenderGraphHandle ? graph.GetImageFormat(depthTarget)
                                                                         : vk::Format::eUndefined;

        const auto traceExtent = ScaledTraceExtent(extent, inputs.Settings.ResolutionScale);
        if (res.TraceExtent.width != traceExtent.width || res.TraceExtent.height != traceExtent.height || !res.RaytraceImage)
        {
            CreateOrResize(res, res.RaytraceImage, traceExtent, "ViewportRaytrace");
            CreateOrResize(res, res.BloomBrightImage, traceExtent, "ViewportBloomBright");
            CreateOrResize(res, res.BloomBlurImages[0], traceExtent, "ViewportBloomBlur0");
            CreateOrResize(res, res.BloomBlurImages[1], traceExtent, "ViewportBloomBlur1");
            CreateOrResize(res, res.LensFlareImage, traceExtent, "ViewportLensFlare");
            res.TraceExtent = traceExtent;
        }
        if (!res.ExposureImage) CreateOrResize(res, res.ExposureImage, vk::Extent3D{1, 1, 1}, "ViewportExposure");
        res.BloomResult = res.BloomBlurImages[0].get();
        res.MeshTrace = {};
        res.TrailsActive = false;
        res.OverlayVertices.clear();

        // A debug view shows the tracer's raw output: no glow, flare, smoothing or tone mapping on top of it
        RenderSettings settings = inputs.Settings;
        if (settings.Debug != DebugView::None)
        {
            settings.Bloom = false;
            settings.BloomDebug = false;
            settings.LensFlare = false;
            settings.Fxaa = false;
            settings.ToneMapping = ToneMap::None;
        }

        RenderFrameContext context{
            .Graph = graph, .Resources = res, .Inputs = inputs, .Settings = settings, .Frame = frame,
            .ViewCamera = camera, .Scene = *inputs.Scene,
            .ColorTarget = colorTarget, .DepthTarget = depthTarget,
            .ColorFormat = graph.GetImageFormat(colorTarget), .DepthFormat = depthFormat,
            .DisplayExtent = extent, .TraceExtent = traceExtent,
            .GroupsX = (traceExtent.width + 15u) / 16u, .GroupsY = (traceExtent.height + 15u) / 16u};

        std::vector<std::string> recorded;
        for (const auto& stage : impl.Stages)
        {
            if (!stage->Enabled(settings)) continue;
            graph.BeginScope(std::string(stage->Name()));
            stage->Record(context);
            graph.EndScope();
            recorded.emplace_back(stage->Name());
        }
        std::scoped_lock lock(impl.RecordedMutex);
        impl.Recorded = std::move(recorded);
    }

    bool RenderPipeline::AllStagesReady() const
    {
        return !m_Impl->Stages.empty() && std::ranges::all_of(m_Impl->Stages, [](const auto& stage) { return stage->Ready(); });
    }

    std::optional<std::string> RenderPipeline::FirstFailure() const
    {
        for (const auto& stage : m_Impl->Stages)
        {
            if (auto failure = stage->Failure()) return failure;
        }
        return std::nullopt;
    }

    const GpuCapabilities& RenderPipeline::Capabilities() const
    {
        return m_Impl->Res.Device->GetCapabilities();
    }

    std::vector<std::string> RenderPipeline::LastRecordedStages() const
    {
        std::scoped_lock lock(m_Impl->RecordedMutex);
        return m_Impl->Recorded;
    }
}
