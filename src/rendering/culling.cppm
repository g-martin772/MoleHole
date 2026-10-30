export module MoleHole:Rendering.Culling;

import std;
import glm;

export namespace MoleHole
{
    struct Frustum
    {
        std::array<glm::vec4, 6> Planes{};
        bool Valid = false;
    };

    // Planes of the clip volume of a Vulkan projection (depth 0..1), normals pointing inwards and normalised
    [[nodiscard]] inline Frustum MakeFrustum(const glm::mat4& viewProjection)
    {
        Frustum frustum;
        const auto row = [&viewProjection](const int i)
        {
            return glm::vec4(viewProjection[0][i], viewProjection[1][i], viewProjection[2][i], viewProjection[3][i]);
        };
        const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        frustum.Planes = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};
        for (auto& plane : frustum.Planes)
        {
            const float length = glm::length(glm::vec3(plane));
            if (!std::isfinite(length) || !std::isfinite(plane.w)) return {};
            if (length > 0.0f) plane /= length;
        }
        frustum.Valid = true;
        return frustum;
    }

    // True unless the sphere (xyz, radius w) lies entirely outside one plane; an unusable frustum or sphere keeps everything
    [[nodiscard]] inline bool SphereInFrustum(const Frustum& frustum, const glm::vec4& sphere)
    {
        if (!frustum.Valid) return true;
        if (!(sphere.w >= 0.0f) || !std::isfinite(sphere.w)) return true;
        if (!std::isfinite(sphere.x) || !std::isfinite(sphere.y) || !std::isfinite(sphere.z)) return true;
        for (const auto& plane : frustum.Planes)
        {
            if (glm::dot(glm::vec3(plane), glm::vec3(sphere)) + plane.w < -sphere.w) return false;
        }
        return true;
    }
}
