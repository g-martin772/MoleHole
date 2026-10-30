module MoleHole;

import :Simulation.Picking;
import std;
import GPP;
import glm;

namespace MoleHole
{
    namespace
    {
        constexpr double kDefaultPickRadiusM = 0.5 * GPP::Units::kSolarGravitationalRadius;
    }

    double BoundingRadiusOf(const GPP::Scene& scene, const entt::entity entity,
                           const BoundingRadiusOverride& radiusOverride)
    {
        if (radiusOverride)
        {
            if (const auto radius = radiusOverride(scene, entity))
            {
                return *radius;
            }
        }

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
        if (const auto* blackHole = registry.try_get<BlackHoleComponent>(entity))
        {
            return GPP::Units::SchwarzschildRadius(blackHole->Mass);
        }
        return kDefaultPickRadiusM;
    }

    std::vector<PickHit> PickAllEntities(
        const GPP::Scene& scene, const glm::dvec3& rayOriginM, const glm::dvec3& rayDirection,
        const BoundingRadiusOverride& radiusOverride)
    {
        const auto direction = glm::normalize(rayDirection);
        std::vector<PickHit> hits;

        for (auto [entity, transform] : scene.Registry().view<GPP::TransformComponent>().each())
        {
            if (IsHidden(scene.Registry(), entity) || IsLocked(scene.Registry(), entity)) continue;
            const double radius = BoundingRadiusOf(scene, entity, radiusOverride);
            const glm::dvec3 toCenter = rayOriginM - transform.Position;
            const double b = glm::dot(toCenter, direction);
            const double c = glm::dot(toCenter, toCenter) - radius * radius;
            const double discriminant = b * b - c;
            if (discriminant < 0.0)
            {
                continue;
            }
            const double sqrtDiscriminant = std::sqrt(discriminant);
            double t = -b - sqrtDiscriminant;
            if (t < 0.0)
            {
                t = -b + sqrtDiscriminant;
            }
            if (t < 0.0)
            {
                continue;
            }
            hits.push_back(PickHit{entity, t});
        }

        std::ranges::sort(hits, {}, &PickHit::Distance);
        return hits;
    }

    std::optional<PickHit> PickClosestEntity(
        const GPP::Scene& scene, const glm::dvec3& rayOriginM, const glm::dvec3& rayDirection,
        const BoundingRadiusOverride& radiusOverride)
    {
        const auto hits = PickAllEntities(scene, rayOriginM, rayDirection, radiusOverride);
        if (hits.empty())
        {
            return std::nullopt;
        }
        return hits.front();
    }

    std::optional<PickHit> CyclePick(const std::vector<PickHit>& hits, const entt::entity current)
    {
        if (hits.empty())
        {
            return std::nullopt;
        }
        const auto found = std::ranges::find(hits, current, &PickHit::Entity);
        if (found == hits.end())
        {
            return hits.front();
        }
        const auto next = std::next(found);
        return next == hits.end() ? hits.front() : *next;
    }

    Ray ScreenPointToRay(const glm::vec2& mousePos, const glm::vec2& viewportMin, const glm::vec2& viewportSize,
                         const glm::mat4& view, const glm::mat4& projection, const glm::dvec3& cameraPositionM,
                         const RenderFrame& frame)
    {
        const glm::vec2 local = mousePos - viewportMin;
        const float ndcX = (2.0f * local.x / viewportSize.x) - 1.0f;
        const float ndcY = (2.0f * local.y / viewportSize.y) - 1.0f;

        const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
        glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        farPoint /= farPoint.w;

        const glm::dvec3 farPointM = frame.ToWorld(glm::vec3(farPoint));
        return Ray{cameraPositionM, glm::normalize(farPointM - cameraPositionM)};
    }

    std::optional<glm::vec2> RenderPointToScreen(const glm::vec3& renderPoint, const glm::vec2& viewportMin,
                                                 const glm::vec2& viewportSize, const glm::mat4& view,
                                                 const glm::mat4& projection)
    {
        const glm::vec4 clip = projection * view * glm::vec4(renderPoint, 1.0f);
        if (!(clip.w > 0.0f))
        {
            return std::nullopt;
        }
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        return viewportMin + (ndc * 0.5f + 0.5f) * viewportSize;
    }

    std::vector<std::uint64_t> EntitiesInScreenRect(const GPP::Scene& scene, const RenderFrame& frame,
                                                    const glm::vec2& viewportMin, const glm::vec2& viewportSize,
                                                    const glm::mat4& view, const glm::mat4& projection,
                                                    const glm::vec2& rectMin, const glm::vec2& rectMax)
    {
        const glm::vec2 low = glm::min(rectMin, rectMax);
        const glm::vec2 high = glm::max(rectMin, rectMax);
        std::vector<std::uint64_t> guids;
        for (auto [entity, transform, metadata] : scene.Registry().view<GPP::TransformComponent, GPP::MetadataComponent>().each())
        {
            if (IsHidden(scene.Registry(), entity) || IsLocked(scene.Registry(), entity)) continue;
            const auto screen = RenderPointToScreen(frame.ToRender(transform.Position), viewportMin, viewportSize, view, projection);
            if (screen && screen->x >= low.x && screen->x <= high.x && screen->y >= low.y && screen->y <= high.y)
            {
                guids.push_back(metadata.Guid);
            }
        }
        return guids;
    }
}
