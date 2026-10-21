module MoleHole;

import :Rendering.Pipeline;
import :Rendering.Data;
import :Rendering.DebugLegend;
import :Rendering.MeshTrace;
import :Rendering.Bodies;
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

        // Everything the tracer kernels read, shared by the camera trace and the body probes
        struct TraceBindings
        {
            RenderResources* Res = nullptr;
            VulkanBuffer* Params = nullptr;
            std::array<VulkanBuffer*, 4> Trails{};
            VulkanBuffer* Bodies = nullptr;
            VulkanBuffer* Lighting = nullptr;
            VulkanBuffer* Lights = nullptr;
            std::array<const VulkanImage*, kBodyTextureSlots> BodyTextures{};
            MeshTraceFrame Meshes;
        };

        // Binding 0 is the kernel's output: the trace image for the camera, the probe image for the probes
        void WriteTraceSet(const TraceBindings& b, const ShaderPipeline& shader, const vk::DescriptorSet set,
                           const vk::ImageView output)
        {
            const auto* res = b.Res;
            DescriptorSetWriter writer(res->Device->GetDevice());
            writer.WriteStorageImage(set, 0, output).WriteUniformBuffer(set, 1, b.Params->GetBuffer());
            const auto bindings = shader.GetReflection().descriptorBindings;
            const auto has = [&bindings](const std::uint32_t binding)
            {
                return std::ranges::any_of(bindings, [binding](const auto& d) { return d.set == 0 && d.binding == binding; });
            };
            const std::array<const VulkanImage*, 4> samplers{
                res->Skybox.get(), res->BlackbodyLut.get(), res->AccelerationLut.get(), res->HrDiagramLut.get()};
            for (std::uint32_t binding = 2; binding < 6; ++binding)
            {
                if (!has(binding)) continue;
                const auto* image = samplers[binding - 2];
                writer.WriteCombinedImageSampler(set, binding, image->GetImageView(), image->GetSampler());
            }
            for (std::uint32_t binding = 9; binding < 13; ++binding)
            {
                if (has(binding)) writer.WriteStorageBuffer(set, binding, b.Trails[binding - 9]->GetBuffer());
            }
            if (has(13)) writer.WriteStorageBuffer(set, 13, b.Bodies->GetBuffer());
            std::array<vk::DescriptorImageInfo, kBodyTextureSlots> bodyImages;
            if (has(14))
            {
                for (std::size_t i = 0; i < bodyImages.size(); ++i)
                {
                    const auto* image = b.BodyTextures[i] ? b.BodyTextures[i] : res->DummyWhiteTexture.get();
                    bodyImages[i] = vk::DescriptorImageInfo{image->GetSampler(), image->GetImageView(),
                                                            vk::ImageLayout::eShaderReadOnlyOptimal};
                }
                writer.WriteCombinedImageSamplers(set, 14, bodyImages);
            }
            if (has(15)) writer.WriteStorageBuffer(set, 15, b.Lighting->GetBuffer());
            if (has(16)) writer.WriteStorageBuffer(set, 16, b.Lights->GetBuffer());
            std::array<vk::DescriptorImageInfo, kMeshTextureSlots> meshImages;
            if (b.Meshes.Active)
            {
                if (has(6)) writer.WriteAccelerationStructure(set, 6, b.Meshes.Tlas);
                if (has(7)) writer.WriteStorageBuffer(set, 7, b.Meshes.Instances);
                if (has(8))
                {
                    for (std::size_t i = 0; i < meshImages.size(); ++i)
                    {
                        const auto* image = b.Meshes.Textures[i] ? b.Meshes.Textures[i] : res->DummyWhiteTexture.get();
                        meshImages[i] = vk::DescriptorImageInfo{image->GetSampler(), image->GetImageView(),
                                                                vk::ImageLayout::eShaderReadOnlyOptimal};
                    }
                    writer.WriteCombinedImageSamplers(set, 8, meshImages);
                }
            }
            writer.Update();
        }

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
                // Bodies sample their maps with non-uniform indices; without descriptor indexing they stay untextured
                if (m_Res.Device->GetCapabilities().DescriptorIndexing) defines["GPP_BODY_TEXTURES"] = "1";
                const auto with = [&defines](std::initializer_list<std::pair<const std::string, std::string>> extra)
                {
                    auto all = defines;
                    all.insert(extra);
                    return all;
                };
                m_Pipeline = MakeComputePipeline(m_Res, "black_hole_rendering.comp", defines);
                m_FlatPipeline = MakeComputePipeline(m_Res, "flat_rendering.comp", defines);
                if (m_Res.Device->SupportsShaderFloat64())
                {
                    m_PipelineF64 = MakeComputePipeline(m_Res, "black_hole_rendering.comp", with({{"GPP_PRECISION_F64", "1"}}));
                }
                m_ProbePipeline = MakeComputePipeline(m_Res, "black_hole_rendering.comp", with({{"GPP_BODY_PROBE", "1"}}));
                if (m_PipelineF64)
                {
                    m_ProbePipelineF64 = MakeComputePipeline(m_Res, "black_hole_rendering.comp",
                                                             with({{"GPP_BODY_PROBE", "1"}, {"GPP_PRECISION_F64", "1"}}));
                }
                m_ReducePipeline = MakeComputePipeline(m_Res, "body_probe_reduce.comp", {});
                if (MeshTraceSupported(m_Res.Device->GetCapabilities()))
                {
                    m_MeshPipeline = MakeComputePipeline(m_Res, "black_hole_rendering.comp", with({{"GPP_MESH_TRACE", "1"}}));
                    if (m_PipelineF64)
                    {
                        m_MeshPipelineF64 = MakeComputePipeline(
                            m_Res, "black_hole_rendering.comp", with({{"GPP_MESH_TRACE", "1"}, {"GPP_PRECISION_F64", "1"}}));
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
                m_BodyBuffer = storage(kMaxSpheres * sizeof(BodyGpu));
                m_LightingBuffer = storage(kMaxSpheres * sizeof(BodyLightingGpu));
                const std::array<BodyLightingGpu, kMaxSpheres> unlit{};
                m_LightingBuffer->Upload(unlit.data(), sizeof(unlit));
                m_LightBuffer = storage(sizeof(LightBlockGpu));

                const vk::Extent3D probeExtent{kProbeWidth, kProbeHeight * kMaxSpheres, 1};
                m_ProbeImage = std::make_unique<VulkanImage>(
                    m_Res.Device,
                    VulkanImageSpecification{.extent = probeExtent, .format = vk::Format::eR32G32B32A32Sfloat,
                                             .usage = vk::ImageUsageFlagBits::eStorage,
                                             .aspectMask = vk::ImageAspectFlagBits::eColor, .debugName = "BodyProbes"},
                    m_Res.Logger);
                ImmediateSubmit(*m_Res.UploadPool, m_Res.Device->GetBackgroundQueue(), [this](const vk::CommandBuffer cmd)
                {
                    TransitionImageLayout(cmd, m_ProbeImage->GetImage(), vk::Format::eR32G32B32A32Sfloat,
                                          vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
                });
            }

            [[nodiscard]] std::string_view Name() const override { return "Trace"; }

            [[nodiscard]] bool Ready() const override
            {
                return Compiled(m_Pipeline) && Compiled(m_FlatPipeline) && (!m_PipelineF64 || Compiled(m_PipelineF64))
                       && MeshPipelinesReady() && Compiled(m_ProbePipeline) && Compiled(m_ReducePipeline)
                       && (!m_ProbePipelineF64 || Compiled(m_ProbePipelineF64))
                       && (!m_Res.Textures || m_Res.Textures->PendingCount() == 0) && m_ProbeFramesSettled;
            }

            [[nodiscard]] std::optional<std::string> Failure() const override
            {
                return CompileFailure(Name(), {&m_Pipeline, &m_FlatPipeline, &m_PipelineF64, &m_MeshPipeline, &m_MeshPipelineF64,
                                               &m_ProbePipeline, &m_ProbePipelineF64, &m_ReducePipeline});
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
                const TraceBindings bindings = MakeBindings();

                RecordProbes(ctx, kernel == TraceKernel::MarcherF64 || kernel == TraceKernel::MarcherMeshesF64, bindings);

                ctx.Graph.AddBudgetedComputePass(
                    "Trace", {}, {}, ctx.GroupsX * ctx.GroupsY,
                    [groupsX, raytracePipeline, bindings, set = vk::DescriptorSet{}](
                        const vk::CommandBuffer cmd, RenderGraph& g, const RenderGraphWorkSlice& slice) mutable
                    {
                        const auto pipeline = raytracePipeline->GetPipeline();
                        if (!pipeline) return;
                        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->GetPipeline());
                        if (!set)
                        {
                            set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                            WriteTraceSet(bindings, *raytracePipeline, set, bindings.Res->RaytraceImage->GetImageView());
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
                UploadBodies(ctx, params);

                params.DebugMode = static_cast<std::int32_t>(render.Debug);
                params.MetricType = static_cast<std::int32_t>(render.Solution);
                params.DiskAccretionRate = render.DiskAccretionRate;
                params.DiskOpacity = render.DiskOpacity;
                params.DiskDisplayTemperature = render.DiskDisplayTemperature;
                params.DiskHotSpots = render.DiskHotSpots;
                FillMetricData(params);
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
                    params.MeshBounds = m_Res.MeshTrace.Bounds;
                    params.NumMeshInstances = m_Res.MeshTrace.Count;
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

            [[nodiscard]] TraceBindings MakeBindings()
            {
                return TraceBindings{
                    .Res = &m_Res, .Params = m_ParamsBuffer.get(),
                    .Trails = {m_TrailSegments.get(), m_TrailCells.get(), m_TrailIndices.get(), m_TrailCoarse.get()},
                    .Bodies = m_BodyBuffer.get(), .Lighting = m_LightingBuffer.get(), .Lights = m_LightBuffer.get(),
                    .BodyTextures = m_BodySlots.Images(), .Meshes = m_Res.MeshTrace};
            }

            // Each body traces the sky at its centre into a band of the probe image, then one workgroup per body
            // reduces it into m_LightingBuffer for the camera trace that follows (and the next frame's probes).
            void RecordProbes(RenderFrameContext& ctx, const bool f64, const TraceBindings& bindings)
            {
                const bool wanted = m_ProbesActive && m_NumSpheres > 0;
                // Exports wait until the probes have run twice: the second pass sees bodies lit by the first
                m_ProbeFramesSettled = !wanted || m_ProbeFrames >= 2;
                if (!wanted)
                {
                    m_ProbeFrames = 0;
                    return;
                }
                const auto probe = f64 && Compiled(m_ProbePipelineF64) ? m_ProbePipelineF64 : m_ProbePipeline;
                if (!Compiled(probe) || !Compiled(m_ReducePipeline)) return;
                ++m_ProbeFrames;
                const auto count = static_cast<std::uint32_t>(m_NumSpheres);
                auto* probeImage = m_ProbeImage.get();
                ctx.Graph.AddComputePass(
                    "BodyProbes", {}, {},
                    [probe, bindings, probeImage, count](const vk::CommandBuffer cmd, RenderGraph& g)
                    {
                        const auto pipeline = probe->GetPipeline();
                        if (!pipeline) return;
                        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->GetPipeline());
                        const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                        WriteTraceSet(bindings, *probe, set, probeImage->GetImageView());
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->GetLayout(), 0, set, {});
                        // The probe main ignores the camera trace's work slicing, so it has no push constants
                        cmd.dispatch(kProbeWidth / 16, count * kProbeHeight / 16, 1);
                    });
                auto reduce = m_ReducePipeline;
                auto* lighting = m_LightingBuffer.get();
                const auto device = m_Res.Device;
                ctx.Graph.AddComputePass(
                    "BodyProbeReduce", {}, {},
                    [reduce, device, probeImage, lighting, count](const vk::CommandBuffer cmd, RenderGraph& g)
                    {
                        const auto pipeline = reduce->GetPipeline();
                        if (!pipeline) return;
                        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->GetPipeline());
                        const auto set = g.AllocateDescriptorSet(pipeline->GetDescriptorSetLayouts()[0]);
                        DescriptorSetWriter(device->GetDevice())
                            .WriteStorageImage(set, 0, probeImage->GetImageView())
                            .WriteStorageBuffer(set, 1, lighting->GetBuffer())
                            .Update();
                        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->GetLayout(), 0, set, {});
                        const auto bodies = static_cast<std::int32_t>(count);
                        cmd.pushConstants(pipeline->GetLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(bodies), &bodies);
                        cmd.dispatch(count, 1, 1);
                    });
            }

            // Surfaces of the spheres FillSceneData uploaded; textures that are still loading are left out for now
            void UploadBodies(const RenderFrameContext& ctx, const RaytraceParamsGpu& params)
            {
                m_NumSpheres = ctx.Settings.RenderSpheres ? params.NumSpheres : 0;
                const bool probe = ctx.Settings.BodyLightingMode == BodyLighting::Probe;
                m_ProbesActive = probe && ctx.Settings.TransportMode != Transport::Straight;
                const auto mode = !probe ? BodyLightingMode::Fixed
                                  : m_ProbesActive ? BodyLightingMode::Probe : BodyLightingMode::LightsOnly;
                auto lights = FillLightData(ctx.Scene, ctx.Frame, mode);
                lights.Header.w = ctx.Settings.BodyAtmospheres ? std::clamp(ctx.Settings.BodyAtmosphereSamples, 1, 64) : 0;
                if (m_NumSpheres > 0 && ctx.Settings.TransportMode != Transport::Straight)
                {
                    const auto local = ChooseLocalFrame(ctx.Scene, ctx.Frame, ctx.ViewCamera.GetPosition());
                    lights.Header.z = local.Sphere;
                    lights.PatchCamera = glm::vec4(local.CameraOffset,
                                                   static_cast<float>(std::clamp(ctx.Settings.TerrainSteps, 8, 512)));
                }
                m_LightBuffer->Upload(&lights, sizeof(lights));

                m_BodySlots.Clear();
                auto* textures = m_Res.Textures.get();
                const bool sampled = m_Res.Device->GetCapabilities().DescriptorIndexing && ctx.Settings.BodyTextures;
                const auto bodies = FillBodyData(ctx.Scene, [&](const std::string& path, const TextureRole role)
                {
                    if (!sampled || path.empty() || !textures) return -1;
                    return m_BodySlots.Assign(textures->Get(path, role));
                }, ctx.Frame.LengthUnitM);
                if (m_BodySlots.Overflowed() && !m_WarnedBodySlots)
                {
                    m_Res.Logger->Warn("Bodies use more than {} textures, the rest are drawn untextured", kBodyTextureSlots);
                    m_WarnedBodySlots = true;
                }
                m_BodyBuffer->Upload(bodies.data(), sizeof(bodies));
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
            std::unique_ptr<VulkanBuffer> m_TrailSegments;
            std::unique_ptr<VulkanBuffer> m_TrailCells;
            std::unique_ptr<VulkanBuffer> m_TrailIndices;
            std::unique_ptr<VulkanBuffer> m_TrailCoarse;
            std::unique_ptr<VulkanBuffer> m_BodyBuffer;
            std::unique_ptr<VulkanBuffer> m_LightingBuffer;
            std::unique_ptr<VulkanBuffer> m_LightBuffer;
            std::unique_ptr<VulkanImage> m_ProbeImage;
            std::shared_ptr<ShaderPipeline> m_ProbePipeline;
            std::shared_ptr<ShaderPipeline> m_ProbePipelineF64;
            std::shared_ptr<ShaderPipeline> m_ReducePipeline;
            int m_NumSpheres = 0;
            bool m_ProbesActive = false;
            int m_ProbeFrames = 0;
            bool m_ProbeFramesSettled = true;
            TextureSlotTable<kBodyTextureSlots> m_BodySlots;
            bool m_WarnedBodySlots = false;
            MeshAccelerator m_Meshes;
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
                struct { std::int32_t Enabled; std::int32_t Auto; float Ev; float Key; } push{
                    ctx.Settings.ToneMapping != ToneMap::None ? 1 : 0,
                    ctx.Settings.Exposure == ExposureMode::Auto ? 1 : 0,
                    ctx.Settings.ExposureEv, ctx.Settings.AutoExposureKey};
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
                    const glm::vec3 axis = glm::length(hole.SpinAxis) > 0.0f ? glm::normalize(hole.SpinAxis) : glm::vec3(0.0f, 1.0f, 0.0f);
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
        res.AccelerationLut = GenerateAccelerationLut(res.Device, *res.UploadPool, queue, res.Logger);
        res.HrDiagramLut = GenerateHrDiagramLut(res.Device, *res.UploadPool, queue, res.Logger);
        res.Skybox = LoadSkyboxTexture(res.Device, *res.UploadPool, queue, res.FileSystem,
                                       "assets/backgrounds/space.hdr", res.Logger);
        res.DummyWhiteTexture = GenerateSolidColorTexture(res.Device, *res.UploadPool, queue, glm::vec4(1.0f), res.Logger);
        res.Textures = std::make_shared<TextureCache>(res.Device, res.FileSystem, res.Logger);
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
        res.Textures->SetMaxSize(static_cast<std::uint32_t>(std::max(inputs.Settings.BodyTextureMaxSize, 0)));
        res.Textures->Update(*res.UploadPool, res.Device->GetBackgroundQueue());
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
