module MoleHole;

import :Simulation.ObjectPaths;
import std;
import GPP;
import glm;

namespace MoleHole
{
    namespace
    {
        void AppendPosition(std::unordered_map<std::uint64_t, std::deque<glm::vec3>>& histories,
                            std::uint64_t guid, const glm::vec3& position, std::size_t maxHistorySize)
        {
            if (guid == 0) return;
            auto& history = histories[guid];
            history.push_back(position);
            while (history.size() > maxHistorySize)
            {
                history.pop_front();
            }
        }
    }

    void ObjectPathTracker::RecordPositions(const GPP::Scene& scene)
    {
        for (auto [entity, mesh, transform] :
             scene.Registry().view<const GPP::MeshComponent, const GPP::TransformComponent>().each())
        {
            AppendPosition(m_MeshHistories, scene.GuidOf(entity), transform.Position, m_MaxHistorySize);
        }

        for (auto [entity, sphere, transform] :
             scene.Registry().view<const SphereComponent, const GPP::TransformComponent>().each())
        {
            AppendPosition(m_SphereHistories, scene.GuidOf(entity), transform.Position, m_MaxHistorySize);
        }
    }
}
