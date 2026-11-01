module;
#include <cstddef>
export module MoleHole:Rendering.MeshTrace;

import std;
import glm;
import GPP;

export namespace MoleHole
{
    // std430, mirrored by MeshInstanceData in shaders/mesh_trace.glsl. Appearance comes from the material table.
    struct MeshInstanceGpu
    {
        glm::mat4 Model{1.0f};
        glm::mat4 NormalMatrix{1.0f};
        std::uint64_t VertexAddress{0};
        std::uint64_t IndexAddress{0};
        std::uint64_t ExtraAddress{0};
        std::uint32_t MaterialIndex{0};
        std::uint32_t _Pad0{0};
    };
    static_assert(sizeof(MeshInstanceGpu) == 160);
    static_assert(offsetof(MeshInstanceGpu, VertexAddress) == 128);
    static_assert(offsetof(MeshInstanceGpu, MaterialIndex) == 152);

    // TLAS instance masks: shadow rays only see the opaque casters, camera rays see everything.
    constexpr std::uint8_t kMeshMaskOpaque = 0x01;
    constexpr std::uint8_t kMeshMaskTransparent = 0x02;
    constexpr std::size_t kMaxMeshClusters = 8;

    [[nodiscard]] inline MeshInstanceGpu MakeMeshInstance(const glm::mat4& model, const std::uint32_t materialIndex,
                                                          const std::uint64_t vertexAddress,
                                                          const std::uint64_t indexAddress,
                                                          const std::uint64_t extraAddress)
    {
        MeshInstanceGpu out;
        out.Model = model;
        out.NormalMatrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(model))));
        out.MaterialIndex = materialIndex;
        out.VertexAddress = vertexAddress;
        out.IndexAddress = indexAddress;
        out.ExtraAddress = extraAddress;
        return out;
    }

    // Instances that need a per-hit decision (alpha test, single-sided culling) are non-opaque for the ray query
    // and invisible to shadow rays when the material is cut out or blended.
    [[nodiscard]] inline std::uint8_t MeshInstanceMask(const GPP::GltfMaterialGpu& material) noexcept
    {
        return material.AlphaMode() == GPP::GltfAlphaMode::Opaque ? kMeshMaskOpaque : kMeshMaskTransparent;
    }

    [[nodiscard]] inline bool MeshInstanceNeedsHitTest(const GPP::GltfMaterialGpu& material) noexcept
    {
        return material.AlphaMode() != GPP::GltfAlphaMode::Opaque || !material.DoubleSided();
    }

    [[nodiscard]] inline glm::vec4 MergeBounds(const glm::vec4& a, const glm::vec4& b)
    {
        if (a.w <= 0.0f) return b;
        if (b.w <= 0.0f) return a;
        const glm::vec3 offset = glm::vec3(b) - glm::vec3(a);
        const float distance = glm::length(offset);
        if (distance + b.w <= a.w) return a;
        if (distance + a.w <= b.w) return b;
        const float radius = 0.5f * (distance + a.w + b.w);
        return {glm::vec3(a) + offset * ((radius - a.w) / distance), radius};
    }

    [[nodiscard]] inline glm::vec4 TransformBounds(const glm::mat4& model, const float radius)
    {
        const float scale = std::max({glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])),
                                      glm::length(glm::vec3(model[2]))});
        return {glm::vec3(model[3]), radius * scale};
    }

    // Median splits keep a few far-apart meshes in tight spheres
    [[nodiscard]] inline std::vector<glm::vec4> ClusterBounds(const std::span<const glm::vec4> bounds,
                                                              const std::size_t maxClusters)
    {
        std::vector<glm::vec4> items;
        items.reserve(bounds.size());
        for (const auto& sphere : bounds)
        {
            if (sphere.w > 0.0f) items.push_back(sphere);
        }
        if (items.size() <= maxClusters || maxClusters == 0) return maxClusters == 0 ? std::vector<glm::vec4>{} : items;

        struct Group
        {
            std::size_t First = 0;
            std::size_t Last = 0;
            glm::vec4 Sphere{0.0f};
        };
        const auto enclose = [&items](const std::size_t first, const std::size_t last)
        {
            glm::vec4 sphere{0.0f};
            for (std::size_t i = first; i < last; ++i) sphere = MergeBounds(sphere, items[i]);
            return sphere;
        };
        std::vector<Group> groups{{0, items.size(), enclose(0, items.size())}};
        while (groups.size() < maxClusters)
        {
            std::size_t pick = groups.size();
            for (std::size_t g = 0; g < groups.size(); ++g)
            {
                if (groups[g].Last - groups[g].First < 2) continue;
                if (pick == groups.size() || groups[g].Sphere.w > groups[pick].Sphere.w) pick = g;
            }
            if (pick == groups.size()) break;
            const Group group = groups[pick];
            glm::vec3 low(std::numeric_limits<float>::max());
            glm::vec3 high(std::numeric_limits<float>::lowest());
            for (std::size_t i = group.First; i < group.Last; ++i)
            {
                low = glm::min(low, glm::vec3(items[i]));
                high = glm::max(high, glm::vec3(items[i]));
            }
            const glm::vec3 extent = high - low;
            const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
            const std::size_t middle = group.First + (group.Last - group.First) / 2;
            std::nth_element(items.begin() + static_cast<std::ptrdiff_t>(group.First),
                             items.begin() + static_cast<std::ptrdiff_t>(middle),
                             items.begin() + static_cast<std::ptrdiff_t>(group.Last),
                             [axis](const glm::vec4& a, const glm::vec4& b) { return a[axis] < b[axis]; });
            groups[pick] = {group.First, middle, enclose(group.First, middle)};
            groups.push_back({middle, group.Last, enclose(middle, group.Last)});
        }
        std::vector<glm::vec4> out;
        out.reserve(groups.size());
        for (const auto& group : groups) out.push_back(group.Sphere);
        return out;
    }
}
