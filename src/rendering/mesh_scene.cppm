module;
#include <cstddef>
#include <gpp/profile.h>
export module MoleHole:Rendering.MeshScene;

import std;
import glm;
import vulkan;
import GPP;
import :Simulation.Components;
import :Rendering.Frame;
import :Rendering.Settings;
import :Rendering.Meshes;
import :Rendering.MeshTrace;
import :Rendering.Lighting;

export namespace MoleHole
{
    struct MeshDraw
    {
        const GPP::GltfPrimitive* Primitive = nullptr;
        std::shared_ptr<GPP::GltfSceneData> Owner;
        glm::mat4 Model{1.0f};
        std::uint32_t MaterialIndex = 0;
    };

    // Everything the two mesh passes read for one frame. Handles stay valid for the lifetime of the MeshScene.
    struct MeshSceneFrame
    {
        std::vector<MeshDraw> Draws;
        bool HasTlas = false;
        vk::AccelerationStructureKHR Tlas{};
        vk::Buffer Instances{};
        vk::Buffer Materials{};
        vk::Buffer Lights{};
        vk::Buffer LightParams{};
        vk::Buffer Dfg{};
        std::shared_ptr<const std::vector<vk::DescriptorImageInfo>> Textures;
        glm::vec4 Bounds{0.0f};
        std::int32_t InstanceCount = 0;
        std::uint32_t TextureCapacity = 0;
        MeshLighting Lighting;
    };

    // Gathers the scene's meshes once per frame for the raster and the traced pass: draws with material indices, the
    // material and light buffers and, when asked, a TLAS over all primitives (traced meshes, shadow rays).
    class MeshScene
    {
    public:
        MeshScene(std::shared_ptr<GPP::VulkanDevice> device, std::shared_ptr<MeshCache> meshes,
                  std::shared_ptr<GPP::Logger> logger, GPP::VulkanCommandPool& uploadPool)
            : m_Device(std::move(device)), m_Meshes(std::move(meshes)), m_Logger(std::move(logger)),
              m_UploadPool(&uploadPool), m_Materials(m_Device, m_Logger)
        {
        }

        [[nodiscard]] const MeshMaterialTable& Materials() const noexcept { return m_Materials; }

        [[nodiscard]] bool ShadowsAvailable() const
        {
            const auto& caps = m_Device->GetCapabilities();
            return caps.RayQuery && caps.AccelerationStructure;
        }

        MeshSceneFrame Prepare(GPP::RenderGraph& graph, const GPP::Scene& scene, const RenderFrame& frame,
                               const RenderSettings& settings, const bool buildTlas, const GPP::VulkanImage& fallbackTexture)
        {
            GPP_ZONE("Mesh.Prepare");
            MeshSceneFrame out;
            auto& records = m_Records;
            auto& instances = m_TlasInstances;
            records.clear();
            instances.clear();
            glm::vec4 bounds{0.0f};

            for (auto [entity, meshComponent, transform] :
                 scene.Registry().view<const GPP::MeshComponent, const GPP::TransformComponent>().each())
            {
                if (meshComponent.AssetPath.empty()) continue;
                const auto gltfScene = m_Meshes->GetOrLoad(meshComponent.AssetPath);
                if (!gltfScene) continue;
                const glm::mat4 model = transform.GetRenderMatrix(frame.OriginM, frame.LengthUnitM, meshComponent.MetresPerUnit);
                const auto base = m_Materials.BaseMaterial(*gltfScene);
                const auto first = out.Draws.size();
                for (const auto& mesh : gltfScene->Meshes)
                {
                    for (const auto& primitive : mesh.Primitives)
                    {
                        if (primitive.IndexCount < 3 || primitive.VertexCount == 0) continue;
                        const bool known = primitive.MaterialIndex >= 0
                                           && static_cast<std::size_t>(primitive.MaterialIndex) < gltfScene->Materials.size();
                        out.Draws.push_back({&primitive, gltfScene, model,
                                             MeshMaterialTable::MaterialIndex(base, known ? primitive.MaterialIndex : -1)});
                    }
                }
                if (out.Draws.size() > first)
                    bounds = MergeBounds(bounds, TransformBounds(model, m_Meshes->BoundingRadius(meshComponent.AssetPath)));
            }
            if (out.Draws.empty()) return out;

            auto& slot = m_Slots[m_NextSlot];
            m_NextSlot = (m_NextSlot + 1) % kSlots;
            m_Materials.Sync(fallbackTexture);
            out.Dfg = DfgBuffer();
            out.Materials = m_Materials.Buffer();
            out.Textures = m_Materials.Textures();
            out.TextureCapacity = m_Materials.TextureCapacity();

            const bool tlas = buildTlas && ShadowsAvailable();
            out.Lighting = BuildMeshLighting(scene, frame, settings, m_DiskCache, tlas);
            UploadLighting(slot, out);

            if (!tlas) return out;
            for (const auto& draw : out.Draws) Cached(draw);
            FlushBlas();
            for (const auto& draw : out.Draws)
            {
                const auto* cached = Cached(draw);
                if (!cached) continue;
                const auto& material = m_Materials.Material(draw.MaterialIndex);
                instances.push_back(GPP::TlasInstance{
                    .Transform = draw.Model, .CustomIndex = static_cast<std::uint32_t>(records.size()),
                    .Mask = MeshInstanceMask(material),
                    .Flags = MeshInstanceNeedsHitTest(material)
                                 ? vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable
                                       | vk::GeometryInstanceFlagBitsKHR::eForceNoOpaque
                                 : vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable,
                    .Blas = cached->Structure->GetDeviceAddress()});
                records.push_back(MakeMeshInstance(draw.Model, draw.MaterialIndex, cached->Vertices, cached->Indices,
                                                   draw.Primitive->ExtraBuffer.GetDeviceAddress()));
            }
            if (records.empty()) return out;

            if (!slot.Tlas) slot.Tlas = std::make_unique<GPP::Tlas>(m_Device);
            slot.Tlas->SetInstances(instances);
            const auto bytes = static_cast<vk::DeviceSize>(records.size() * sizeof(MeshInstanceGpu));
            if (!slot.Records || slot.Records->GetSize() < bytes)
            {
                if (slot.Records) m_RetiredBuffers.push_back(std::move(slot.Records));
                slot.Records = std::make_shared<GPP::VulkanBuffer>(
                    m_Device, GPP::MakeStorageBufferSpecification(std::bit_ceil(bytes), true), m_Logger);
            }
            slot.Records->Upload(records.data(), bytes);

            const auto previousScope = graph.CurrentScope();
            graph.BeginScope("MeshAccel");
            graph.AddComputePass("MeshAccel", {}, {}, [tlas = slot.Tlas.get()](const vk::CommandBuffer cmd, GPP::RenderGraph&)
            {
                tlas->RecordBuild(cmd);
            });
            graph.BeginScope(previousScope);

            out.HasTlas = true;
            out.Tlas = slot.Tlas->GetHandle();
            out.Instances = slot.Records->GetBuffer();
            out.Bounds = glm::vec4(glm::vec3(bounds), bounds.w * 1.01f + 1e-4f);
            out.InstanceCount = static_cast<std::int32_t>(records.size());
            return out;
        }

    private:
        struct CachedPrimitive
        {
            std::shared_ptr<GPP::GltfSceneData> Owner;
            std::unique_ptr<GPP::Blas> Structure;
            vk::DeviceAddress Vertices = 0;
            vk::DeviceAddress Indices = 0;
        };

        // Every Prepare owns one slot of a ring: several render targets and frames in flight must not overwrite
        // the instance, light and TLAS data a previous submission is still reading.
        static constexpr std::size_t kSlots = 8;

        struct Slot
        {
            std::unique_ptr<GPP::Tlas> Tlas;
            std::shared_ptr<GPP::VulkanBuffer> Records;
            std::unique_ptr<GPP::VulkanBuffer> Lights;
            std::unique_ptr<GPP::VulkanBuffer> Params;
        };

        void UploadLighting(Slot& slot, MeshSceneFrame& out)
        {
            if (!slot.Lights)
            {
                slot.Lights = std::make_unique<GPP::VulkanBuffer>(
                    m_Device, GPP::MakeStorageBufferSpecification(sizeof(GPP::LightsGpu), true), m_Logger);
                slot.Params = std::make_unique<GPP::VulkanBuffer>(
                    m_Device, GPP::MakeUniformBufferSpecification(sizeof(MeshLightingParamsGpu)), m_Logger);
            }
            slot.Lights->Upload(&out.Lighting.Lights, out.Lighting.Lights.UsedBytes());
            slot.Params->Upload(&out.Lighting.Params, sizeof(MeshLightingParamsGpu));
            out.Lights = slot.Lights->GetBuffer();
            out.LightParams = slot.Params->GetBuffer();
        }

        // GPP::Pbr::BuildDfgTable at the size the shader expects (MESH_DFG_SIZE)
        vk::Buffer DfgBuffer()
        {
            if (!m_Dfg)
            {
                constexpr int size = 32;
                const auto table = GPP::Pbr::BuildDfgTable(size, size);
                std::vector<glm::vec4> texels;
                texels.reserve(table.Texels().size());
                for (const auto& texel : table.Texels()) texels.emplace_back(texel);
                const auto bytes = static_cast<vk::DeviceSize>(texels.size() * sizeof(glm::vec4));
                m_Dfg = std::make_unique<GPP::VulkanBuffer>(m_Device, GPP::MakeStorageBufferSpecification(bytes, true), m_Logger);
                m_Dfg->Upload(texels.data(), bytes);
            }
            return m_Dfg->GetBuffer();
        }

        // Creates the BLAS without building it; FlushBlas builds everything created since in one submit
        const CachedPrimitive* Cached(const MeshDraw& draw)
        {
            const auto& primitive = *draw.Primitive;
            if (const auto it = m_Primitives.find(&primitive); it != m_Primitives.end())
                return it->second.Structure ? &it->second : nullptr;
            CachedPrimitive entry{.Owner = draw.Owner};
            try
            {
                entry.Vertices = primitive.VertexBuffer.GetDeviceAddress();
                entry.Indices = primitive.IndexBuffer.GetDeviceAddress();
                entry.Structure = std::make_unique<GPP::Blas>(
                    m_Device,
                    GPP::BlasTriangles{.VertexAddress = entry.Vertices, .VertexStride = sizeof(GPP::GltfVertex),
                                       .VertexCount = primitive.VertexCount, .IndexAddress = entry.Indices,
                                       .IndexCount = primitive.IndexCount});
                m_UnbuiltBlas.push_back(&primitive);
            }
            catch (const std::exception& error)
            {
                m_Logger->Warn("Mesh tracing: BLAS build failed: {}", error.what());
                entry.Structure.reset();
            }
            const auto& stored = m_Primitives.emplace(&primitive, std::move(entry)).first->second;
            return stored.Structure ? &stored : nullptr;
        }

        void FlushBlas()
        {
            if (m_UnbuiltBlas.empty()) return;
            std::vector<GPP::Blas*> blases;
            blases.reserve(m_UnbuiltBlas.size());
            for (const auto* primitive : m_UnbuiltBlas) blases.push_back(m_Primitives.at(primitive).Structure.get());
            try
            {
                GPP::BuildBlasBatch(m_Device, *m_UploadPool, m_Device->GetBackgroundQueue(), blases);
            }
            catch (const std::exception& error)
            {
                m_Logger->Warn("Mesh tracing: BLAS build failed: {}", error.what());
                for (const auto* primitive : m_UnbuiltBlas) m_Primitives.at(primitive).Structure.reset();
            }
            m_UnbuiltBlas.clear();
        }

        std::shared_ptr<GPP::VulkanDevice> m_Device;
        std::shared_ptr<MeshCache> m_Meshes;
        std::shared_ptr<GPP::Logger> m_Logger;
        GPP::VulkanCommandPool* m_UploadPool;
        MeshMaterialTable m_Materials;
        DiskLightCache m_DiskCache;
        std::unordered_map<const GPP::GltfPrimitive*, CachedPrimitive> m_Primitives;
        std::vector<const GPP::GltfPrimitive*> m_UnbuiltBlas;
        std::vector<MeshInstanceGpu> m_Records;
        std::vector<GPP::TlasInstance> m_TlasInstances;
        std::unique_ptr<GPP::VulkanBuffer> m_Dfg;
        std::array<Slot, kSlots> m_Slots;
        std::size_t m_NextSlot = 0;
        std::vector<std::shared_ptr<GPP::VulkanBuffer>> m_RetiredBuffers;
    };
}
