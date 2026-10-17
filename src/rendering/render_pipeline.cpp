module MoleHole;

import :Rendering.Pipeline;
import :Rendering.Data;
import :Rendering.DebugLegend;
import :Rendering.MeshTrace;
import :Rendering.Lut;
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

        struct MeshDrawable
        {
            const GltfPrimitive* Primitive = nullptr;
            glm::mat4 Model{1.0f};
            glm::vec4 BaseColorFactor{0.8f, 0.8f, 0.8f, 1.0f};
            float MetallicFactor{0.5f};
            float RoughnessFactor{0.5f};
            std::shared_ptr<VulkanImage> BaseColorTexture;
        };

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
                                                           std::string fragment, const char* what)
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
                        .stage = ShaderStage::Fragment
                    },
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

        class MeshAccelerator
        {
        public:
            explicit MeshAccelerator(RenderResources& res) : m_Res(res) {}

            void Record(const RenderFrameContext& ctx)
            {
                m_Textures.Clear();
                std::vector<MeshInstanceGpu> records;
                std::vector<TlasInstance> instances;
                glm::vec4 bounds{0.0f};
                for (auto [entity, meshComponent, transform] :
                     ctx.Scene.Registry().view<const MeshComponent, const TransformComponent>().each())
                {
                    if (meshComponent.AssetPath.empty()) continue;
                    const auto gltfScene = m_Res.Meshes->GetOrLoad(meshComponent.AssetPath);
                    if (!gltfScene) continue;
                    const glm::mat4 model = transform.GetRenderMatrix(ctx.Frame.OriginM, ctx.Frame.LengthUnitM,
                                                                      meshComponent.MetresPerUnit);
                    const auto first = records.size();
                    for (const auto& mesh : gltfScene->Meshes)
                    {
                        for (const auto& primitive : mesh.Primitives)
                        {
                            const auto* cached = Cached(gltfScene, primitive);
                            if (!cached) continue;
                            const GltfMaterial* material = nullptr;
                            if (primitive.MaterialIndex >= 0 && static_cast<std::size_t>(primitive.MaterialIndex)
                                                                    < gltfScene->Materials.size())
                                material = &gltfScene->Materials[static_cast<std::size_t>(primitive.MaterialIndex)];
                            const int slot = material ? m_Textures.Assign(material->BaseColorTexture.get()) : -1;
                            instances.push_back(TlasInstance{
                                .Transform = model, .CustomIndex = static_cast<std::uint32_t>(records.size()),
                                .Blas = cached->Structure->GetDeviceAddress()});
                            records.push_back(MakeMeshInstance(model, material, slot, cached->Vertices, cached->Indices));
                        }
                    }
                    if (records.size() > first)
                        bounds = MergeBounds(bounds, TransformBounds(model, m_Res.Meshes->BoundingRadius(meshComponent.AssetPath)));
                }
                if (records.empty()) return;
                if (m_Textures.Overflowed() && !m_WarnedTextures)
                {
                    m_Res.Logger->Warn("Mesh tracing: more than {} base colour textures, the rest are drawn untextured",
                                       kMeshTextureSlots);
                    m_WarnedTextures = true;
                }

                if (!m_Tlas) m_Tlas = std::make_unique<Tlas>(m_Res.Device);
                m_Tlas->SetInstances(instances);
                const auto bytes = static_cast<vk::DeviceSize>(records.size() * sizeof(MeshInstanceGpu));
                if (!m_Records || m_Records->GetSize() < bytes)
                {
                    m_Records = std::make_unique<VulkanBuffer>(
                        m_Res.Device, MakeStorageBufferSpecification(std::bit_ceil(bytes), true), m_Res.Logger);
                }
                m_Records->Upload(records.data(), bytes);

                ctx.Graph.AddComputePass("MeshAccel", {}, {}, [tlas = m_Tlas.get()](const vk::CommandBuffer cmd, RenderGraph&)
                {
                    tlas->RecordBuild(cmd);
                });

                auto& out = m_Res.MeshTrace;
                out.Active = true;
                out.Tlas = m_Tlas->GetHandle();
                out.Instances = m_Records->GetBuffer();
                out.Textures = m_Textures.Images();
                out.Bounds = glm::vec4(glm::vec3(bounds), bounds.w * 1.01f + 1e-4f);
                out.Count = static_cast<std::int32_t>(records.size());
            }

        private:
            struct CachedPrimitive
            {
                std::shared_ptr<GltfSceneData> Owner;
                std::unique_ptr<Blas> Structure;
                vk::DeviceAddress Vertices = 0;
                vk::DeviceAddress Indices = 0;
            };

            const CachedPrimitive* Cached(const std::shared_ptr<GltfSceneData>& owner, const GltfPrimitive& primitive)
            {
                if (const auto it = m_Primitives.find(&primitive); it != m_Primitives.end())
                    return it->second.Structure ? &it->second : nullptr;
                CachedPrimitive entry{.Owner = owner};
                if (primitive.IndexCount >= 3 && primitive.VertexCount > 0)
                {
                    try
                    {
                        entry.Vertices = primitive.VertexBuffer.GetDeviceAddress();
                        entry.Indices = primitive.IndexBuffer.GetDeviceAddress();
                        entry.Structure = std::make_unique<Blas>(
                            m_Res.Device, *m_Res.UploadPool, m_Res.Device->GetBackgroundQueue(),
                            BlasTriangles{.VertexAddress = entry.Vertices, .VertexStride = sizeof(GltfVertex),
                                          .VertexCount = primitive.VertexCount, .IndexAddress = entry.Indices,
                                          .IndexCount = primitive.IndexCount});
                    }
                    catch (const std::exception& error)
                    {
                        m_Res.Logger->Warn("Mesh tracing: BLAS build failed: {}", error.what());
                        entry.Structure.reset();
                    }
                }
                const auto& stored = m_Primitives.emplace(&primitive, std::move(entry)).first->second;
                return stored.Structure ? &stored : nullptr;
            }

            RenderResources& m_Res;
            std::unordered_map<const GltfPrimitive*, CachedPrimitive> m_Primitives;
            std::unique_ptr<Tlas> m_Tlas;
            std::unique_ptr<VulkanBuffer> m_Records;
            MeshTextureSlots m_Textures;
            bool m_WarnedTextures = false;
        };

        class TraceStage final : public IRenderStage
        {
        public:
            TraceStage(RenderResources& res, const std::chrono::steady_clock::time_point& start)
                : m_Res(res), m_StartTime(start), m_Meshes(res)
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
                    m_MeshPipeline = MakeComputePipeline(m_Res, "black_hole_rendering.comp", {{"GPP_MESH_TRACE", "1"}});
                    if (m_PipelineF64)
                    {
                        m_MeshPipelineF64 = MakeComputePipeline(
                            m_Res, "black_hole_rendering.comp", {{"GPP_MESH_TRACE", "1"}, {"GPP_PRECISION_F64", "1"}});
                    }
                }
                m_ParamsBuffer = std::make_unique<VulkanBuffer>(
                    m_Res.Device, MakeUniformBufferSpecification(sizeof(RaytraceParamsGpu)), m_Res.Logger);
            }

            [[nodiscard]] std::string_view Name() const override { return "Trace"; }

            [[nodiscard]] bool Ready() const override
            {
                return Compiled(m_Pipeline) && Compiled(m_FlatPipeline) && (!m_PipelineF64 || Compiled(m_PipelineF64))
                       && MeshPipelinesReady();
            }

            void Record(RenderFrameContext& ctx) override
            {
                if (m_MeshPipeline && WantsTracedMeshes(ctx.Settings) && MeshPipelinesReady()) m_Meshes.Record(ctx);
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

                ctx.Graph.AddBudgetedComputePass(
                    "Trace", {}, {}, ctx.GroupsX * ctx.GroupsY,
                    [device, groupsX, raytracePipeline, res, params, meshes = m_Res.MeshTrace, set = vk::DescriptorSet{}](
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
                                res->Skybox.get(), res->BlackbodyLut.get(), res->AccelerationLut.get(), res->HrDiagramLut.get()};
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
                            if (meshes.Active)
                            {
                                if (has(6)) writer.WriteAccelerationStructure(set, 6, meshes.Tlas);
                                if (has(7)) writer.WriteStorageBuffer(set, 7, meshes.Instances);
                                if (has(8))
                                {
                                    std::array<vk::DescriptorImageInfo, kMeshTextureSlots> textures;
                                    for (std::size_t i = 0; i < textures.size(); ++i)
                                    {
                                        const auto* image = meshes.Textures[i] ? meshes.Textures[i] : res->DummyWhiteTexture.get();
                                        textures[i] = vk::DescriptorImageInfo{image->GetSampler(), image->GetImageView(),
                                                                              vk::ImageLayout::eShaderReadOnlyOptimal};
                                    }
                                    writer.WriteCombinedImageSamplers(set, 8, textures);
                                }
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
                params.Time = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_StartTime).count();
                params.ViewProjection = camera.GetViewProjectionMatrix(ctx.Frame);
                FillSceneData(params, ctx.Scene, ctx.Frame);

                params.DebugMode = static_cast<std::int32_t>(render.Debug);
                params.MetricType = static_cast<std::int32_t>(render.Solution);
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
                    params.MeshBounds = m_Res.MeshTrace.Bounds;
                    params.NumMeshInstances = m_Res.MeshTrace.Count;
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
            MeshAccelerator m_Meshes;
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
                        } push{settings.Fxaa ? 1 : 0, settings.Bloom ? 1 : 0, settings.BloomIntensity,
                               settings.BloomDebug ? 1 : 0, settings.LensFlare ? 1 : 0, settings.LensFlareIntensity,
                               static_cast<float>(traceExtent.width), static_cast<float>(traceExtent.height)};
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

        class ObjectPathStage final : public IRenderStage
        {
        public:
            ObjectPathStage(RenderResources& res, DebugLines& lines, ObjectPathTracker& tracker, std::mutex& mutex)
                : m_Res(res), m_Lines(lines), m_Tracker(tracker), m_Mutex(mutex)
            {
            }

            [[nodiscard]] std::string_view Name() const override { return "ObjectPaths"; }
            [[nodiscard]] bool Enabled(const RenderSettings& settings) const override { return settings.ShowObjectPaths; }

            void Record(RenderFrameContext& ctx) override
            {
                static const glm::vec3 kMeshColor{0.2f, 0.8f, 0.2f};
                static const glm::vec3 kSphereColor{0.8f, 0.2f, 0.8f};

                std::vector<float> vertices;
                {
                    std::scoped_lock lock(m_Mutex);
                    const auto append = [&](const auto& histories, const glm::vec3& color)
                    {
                        for (const auto& [guid, history] : histories)
                        {
                            for (std::size_t i = 1; i < history.size(); ++i)
                            {
                                const glm::vec3 a = ctx.Frame.ToRender(history[i - 1]);
                                const glm::vec3 b = ctx.Frame.ToRender(history[i]);
                                vertices.insert(vertices.end(), {a.x, a.y, a.z, color.r, color.g, color.b,
                                                                 b.x, b.y, b.z, color.r, color.g, color.b});
                            }
                        }
                    };
                    append(m_Tracker.MeshHistories(), kMeshColor);
                    append(m_Tracker.SphereHistories(), kSphereColor);
                }
                if (vertices.empty()) return;

                m_Lines.EnsurePipeline(m_Res, ctx.ColorFormat);
                if (!m_Lines.Pipeline) return;
                m_Lines.Upload(m_Res, m_Buffer, vertices);
                // 6 floats (pos+color) per vertex, 2 vertices per segment
                RecordLinePass(ctx, "ObjectPaths", m_Lines, m_Buffer.get(), static_cast<std::uint32_t>(vertices.size() / 6));
            }

        private:
            RenderResources& m_Res;
            DebugLines& m_Lines;
            ObjectPathTracker& m_Tracker;
            std::mutex& m_Mutex;
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
                if (!m_Pipeline || m_ColorFormat != ctx.ColorFormat || m_DepthFormat != ctx.DepthFormat)
                {
                    m_Pipeline = MakeRasterPipeline(m_Res, ctx.ColorFormat, ctx.DepthFormat, false,
                                                    vk::PrimitiveTopology::eTriangleList, "mesh.vert", "mesh.frag",
                                                    "mesh overlay");
                    m_ColorFormat = ctx.ColorFormat;
                    m_DepthFormat = ctx.DepthFormat;
                }
                auto drawables = Collect(ctx);
                if (!m_Pipeline || drawables.empty()) return;

                MeshCameraParamsGpu params{};
                params.View = ctx.ViewCamera.GetViewMatrix(ctx.Frame);
                params.Projection = ctx.ViewCamera.GetProjectionMatrix();
                params.CameraPos = ctx.Frame.ToRender(ctx.ViewCamera.GetPosition());
                params.LightDir = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
                m_CameraBuffer->Upload(&params, sizeof(params));

                const auto device = m_Res.Device;
                const auto meshPipeline = m_Pipeline;
                auto* cameraBuffer = m_CameraBuffer.get();
                auto* white = m_Res.DummyWhiteTexture.get();
                ctx.Graph.AddGraphicsPass(
                    "Meshes", {}, {},
                    {RenderGraphAttachment{.Handle = ctx.ColorTarget, .LoadOp = vk::AttachmentLoadOp::eLoad}},
                    RenderGraphAttachment{.Handle = ctx.DepthTarget, .LoadOp = vk::AttachmentLoadOp::eLoad},
                    [device, meshPipeline, cameraBuffer, white, drawables = std::move(drawables)](
                        const vk::CommandBuffer cmd, RenderGraph& g)
                    {
                        const auto pipeline = meshPipeline->GetPipeline();
                        if (!pipeline) return;
                        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->GetPipeline());
                        for (const auto& drawable : drawables)
                        {
                            const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                            const auto* texture = drawable.BaseColorTexture ? drawable.BaseColorTexture.get() : white;
                            DescriptorSetWriter(device->GetDevice())
                                .WriteUniformBuffer(set, 0, cameraBuffer->GetBuffer())
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

        private:
            std::vector<MeshDrawable> Collect(const RenderFrameContext& ctx)
            {
                std::vector<MeshDrawable> drawables;
                for (auto [entity, meshComponent, transform] :
                     ctx.Scene.Registry().view<const MeshComponent, const TransformComponent>().each())
                {
                    if (meshComponent.AssetPath.empty()) continue;
                    const auto gltfScene = m_Res.Meshes->GetOrLoad(meshComponent.AssetPath);
                    if (!gltfScene) continue;
                    const glm::mat4 model = transform.GetRenderMatrix(ctx.Frame.OriginM, ctx.Frame.LengthUnitM,
                                                                      meshComponent.MetresPerUnit);
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

        auto trace = std::make_unique<TraceStage>(res, impl.StartTime);
        trace->Initialize();
        impl.Stages.push_back(std::move(trace));
        impl.Stages.push_back(std::make_unique<BloomStage>(res));
        impl.Stages.push_back(std::make_unique<LensFlareStage>(res));
        impl.Stages.push_back(std::make_unique<CompositeStage>(res));
        impl.Stages.push_back(std::make_unique<GravityGridStage>(res));
        impl.Stages.push_back(std::make_unique<PhysicsDebugStage>(res, impl.Lines));
        impl.Stages.push_back(std::make_unique<ObjectPathStage>(res, impl.Lines, impl.Paths, impl.PathMutex));
        impl.Stages.push_back(std::make_unique<MeshStage>(res));

        res.BlackbodyLut = GenerateBlackbodyLut(res.Device, *res.UploadPool, queue, res.Logger);
        res.AccelerationLut = GenerateAccelerationLut(res.Device, *res.UploadPool, queue, res.Logger);
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
        res.BloomResult = res.BloomBlurImages[0].get();
        res.MeshTrace = {};

        // A debug view shows the tracer's raw output: no glow, flare or smoothing on top of it
        RenderSettings settings = inputs.Settings;
        if (settings.Debug != DebugView::None)
        {
            settings.Bloom = false;
            settings.BloomDebug = false;
            settings.LensFlare = false;
            settings.Fxaa = false;
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
