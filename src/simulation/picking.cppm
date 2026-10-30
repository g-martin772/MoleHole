export module MoleHole:Simulation.Picking;

import std;
import GPP;
import glm;
import :Simulation.Components;
import :Rendering.Frame;

export namespace MoleHole
{
    struct PickHit
    {
        entt::entity Entity{entt::null};
        double Distance{0.0}; // m
    };

    using BoundingRadiusOverride = std::function<std::optional<double>(const GPP::Scene&, entt::entity)>;

    // Metres.
    [[nodiscard]] double BoundingRadiusOf(const GPP::Scene& scene, entt::entity entity,
                                          const BoundingRadiusOverride& radiusOverride = {});

    [[nodiscard]] std::optional<PickHit> PickClosestEntity(
        const GPP::Scene& scene, const glm::dvec3& rayOriginM, const glm::dvec3& rayDirection,
        const BoundingRadiusOverride& radiusOverride = {});

    [[nodiscard]] std::vector<PickHit> PickAllEntities(
        const GPP::Scene& scene, const glm::dvec3& rayOriginM, const glm::dvec3& rayDirection,
        const BoundingRadiusOverride& radiusOverride = {});

    [[nodiscard]] std::optional<PickHit> CyclePick(const std::vector<PickHit>& hits, entt::entity current);

    struct Ray
    {
        glm::dvec3 OriginM{0.0};
        glm::dvec3 Direction{0.0, 0.0, -1.0};
    };

    // unprojects a mouse position (in window/screen coordinates) through the given viewport
    // rect and render-space camera matrices into a world-space (SI) ray
    [[nodiscard]] Ray ScreenPointToRay(const glm::vec2& mousePos, const glm::vec2& viewportMin,
                                        const glm::vec2& viewportSize, const glm::mat4& view,
                                        const glm::mat4& projection, const glm::dvec3& cameraPositionM,
                                        const RenderFrame& frame);

    [[nodiscard]] std::optional<glm::vec2> RenderPointToScreen(const glm::vec3& renderPoint, const glm::vec2& viewportMin,
                                                               const glm::vec2& viewportSize, const glm::mat4& view,
                                                               const glm::mat4& projection);

    [[nodiscard]] std::vector<std::uint64_t> EntitiesInScreenRect(const GPP::Scene& scene, const RenderFrame& frame,
                                                                  const glm::vec2& viewportMin, const glm::vec2& viewportSize,
                                                                  const glm::mat4& view, const glm::mat4& projection,
                                                                  const glm::vec2& rectMin, const glm::vec2& rectMax);
}
