export module MoleHole:Simulation.Picking;

import std;
import GPP;
import glm;
import :Simulation.Components;

export namespace MoleHole
{
    struct PickHit
    {
        entt::entity Entity{entt::null};
        float Distance{0.0f};
    };

    using BoundingRadiusOverride = std::function<std::optional<float>(const GPP::Scene&, entt::entity)>;

    [[nodiscard]] float BoundingRadiusOf(const GPP::Scene& scene, entt::entity entity,
                                          const BoundingRadiusOverride& radiusOverride = {});

    [[nodiscard]] std::optional<PickHit> PickClosestEntity(
        const GPP::Scene& scene, const glm::vec3& rayOrigin, const glm::vec3& rayDirection,
        const BoundingRadiusOverride& radiusOverride = {});

    struct Ray
    {
        glm::vec3 Origin{0.0f};
        glm::vec3 Direction{0.0f, 0.0f, -1.0f};
    };

    // unprojects a mouse position (in window/screen coordinates) through the given viewport
    // rect and camera matrices into a world-space ray
    [[nodiscard]] Ray ScreenPointToRay(const glm::vec2& mousePos, const glm::vec2& viewportMin,
                                        const glm::vec2& viewportSize, const glm::mat4& view,
                                        const glm::mat4& projection, const glm::vec3& cameraPosition);
}
