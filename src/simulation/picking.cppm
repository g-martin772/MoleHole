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

    [[nodiscard]] float BoundingRadiusOf(const GPP::Scene& scene, entt::entity entity);

    [[nodiscard]] std::optional<PickHit> PickClosestEntity(
        const GPP::Scene& scene, const glm::vec3& rayOrigin, const glm::vec3& rayDirection);
}
