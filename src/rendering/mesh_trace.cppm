module;
#include <cstddef>
export module MoleHole:Rendering.MeshTrace;

import std;
import glm;
import GPP;

export namespace MoleHole
{
    constexpr std::size_t kMeshTextureSlots = 32;

    struct MeshInstanceGpu
    {
        glm::mat4 Model{1.0f};
        glm::mat4 NormalMatrix{1.0f};
        glm::vec4 BaseColorFactor{0.8f, 0.8f, 0.8f, 1.0f};
        std::uint64_t VertexAddress{0};
        std::uint64_t IndexAddress{0};
        float MetallicFactor{0.5f};
        float RoughnessFactor{0.5f};
        std::int32_t TextureSlot{-1};
        std::int32_t _Pad0{0};
    };
    static_assert(sizeof(MeshInstanceGpu) == 176);
    static_assert(offsetof(MeshInstanceGpu, BaseColorFactor) == 128);
    static_assert(offsetof(MeshInstanceGpu, VertexAddress) == 144);
    static_assert(offsetof(MeshInstanceGpu, MetallicFactor) == 160);

    [[nodiscard]] inline MeshInstanceGpu MakeMeshInstance(const glm::mat4& model, const GPP::GltfMaterial* material,
                                                          const int textureSlot, const std::uint64_t vertexAddress,
                                                          const std::uint64_t indexAddress)
    {
        MeshInstanceGpu out;
        out.Model = model;
        out.NormalMatrix = glm::mat4(glm::transpose(glm::inverse(glm::mat3(model))));
        if (material)
        {
            out.BaseColorFactor = material->BaseColorFactor;
            out.MetallicFactor = material->MetallicFactor;
            out.RoughnessFactor = material->RoughnessFactor;
        }
        out.TextureSlot = textureSlot;
        out.VertexAddress = vertexAddress;
        out.IndexAddress = indexAddress;
        return out;
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

    class MeshTextureSlots
    {
    public:
        int Assign(const GPP::VulkanImage* image)
        {
            if (!image) return -1;
            for (std::size_t i = 0; i < m_Count; ++i)
            {
                if (m_Images[i] == image) return static_cast<int>(i);
            }
            if (m_Count == kMeshTextureSlots)
            {
                m_Overflowed = true;
                return -1;
            }
            m_Images[m_Count] = image;
            return static_cast<int>(m_Count++);
        }

        void Clear()
        {
            m_Images.fill(nullptr);
            m_Count = 0;
            m_Overflowed = false;
        }

        [[nodiscard]] const std::array<const GPP::VulkanImage*, kMeshTextureSlots>& Images() const noexcept { return m_Images; }
        [[nodiscard]] std::size_t Count() const noexcept { return m_Count; }
        [[nodiscard]] bool Overflowed() const noexcept { return m_Overflowed; }

    private:
        std::array<const GPP::VulkanImage*, kMeshTextureSlots> m_Images{};
        std::size_t m_Count = 0;
        bool m_Overflowed = false;
    };
}
