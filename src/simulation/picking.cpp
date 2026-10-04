module MoleHole;

import :Simulation.Picking;
import std;
import GPP;
import glm;

namespace MoleHole
{
    float BoundingRadiusOf(const GPP::Scene& scene, const entt::entity entity)
    {
        const auto& registry = scene.Registry();
        if (registry.all_of<SphereComponent>(entity))
        {
            return registry.get<SphereComponent>(entity).Radius;
        }
        if (registry.all_of<GPP::ColliderComponent>(entity))
        {
            const auto& collider = registry.get<GPP::ColliderComponent>(entity);
            switch (collider.Shape)
            {
            case GPP::ColliderShape::Sphere:
            case GPP::ColliderShape::Capsule:
                return collider.Radius;
            case GPP::ColliderShape::Box:
                return glm::length(collider.HalfExtents);
            default:
                return collider.Radius;
            }
        }
        if (registry.all_of<BlackHoleComponent>(entity))
        {
            return 1.0f;
        }
        return 0.5f;
    }

    std::optional<PickHit> PickClosestEntity(
        const GPP::Scene& scene, const glm::vec3& rayOrigin, const glm::vec3& rayDirection)
    {
        const auto direction = glm::normalize(rayDirection);
        std::optional<PickHit> closest;

        for (auto [entity, transform] : scene.Registry().view<GPP::TransformComponent>().each())
        {
            const float radius = BoundingRadiusOf(scene, entity);
            const glm::vec3 toCenter = rayOrigin - transform.Position;
            const float b = glm::dot(toCenter, direction);
            const float c = glm::dot(toCenter, toCenter) - radius * radius;
            const float discriminant = b * b - c;
            if (discriminant < 0.0f)
            {
                continue;
            }
            const float sqrtDiscriminant = std::sqrt(discriminant);
            float t = -b - sqrtDiscriminant;
            if (t < 0.0f)
            {
                t = -b + sqrtDiscriminant;
            }
            if (t < 0.0f)
            {
                continue;
            }
            if (!closest || t < closest->Distance)
            {
                closest = PickHit{entity, t};
            }
        }

        return closest;
    }

    Ray ScreenPointToRay(const glm::vec2& mousePos, const glm::vec2& viewportMin, const glm::vec2& viewportSize,
                         const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPosition)
    {
        const glm::vec2 local = mousePos - viewportMin;
        const float ndcX = (2.0f * local.x / viewportSize.x) - 1.0f;
        const float ndcY = (2.0f * local.y / viewportSize.y) - 1.0f;

        const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
        glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        farPoint /= farPoint.w;

        return Ray{cameraPosition, glm::normalize(glm::vec3(farPoint) - cameraPosition)};
    }
}
