export module MoleHole:Simulation.ObjectPaths;

import std;
import glm;
import GPP;
import :Simulation.Components;

export namespace MoleHole
{
    class ObjectPathTracker
    {
    public:
        void RecordPositions(const GPP::Scene& scene);

        void Clear() noexcept
        {
            m_MeshHistories.clear();
            m_SphereHistories.clear();
        }

        void SetMaxHistorySize(std::size_t size) noexcept { m_MaxHistorySize = size; }
        [[nodiscard]] std::size_t MaxHistorySize() const noexcept { return m_MaxHistorySize; }

        [[nodiscard]] const std::unordered_map<std::uint64_t, std::deque<glm::vec3>>& MeshHistories() const noexcept
        {
            return m_MeshHistories;
        }

        [[nodiscard]] const std::unordered_map<std::uint64_t, std::deque<glm::vec3>>& SphereHistories() const noexcept
        {
            return m_SphereHistories;
        }

    private:
        std::unordered_map<std::uint64_t, std::deque<glm::vec3>> m_MeshHistories;
        std::unordered_map<std::uint64_t, std::deque<glm::vec3>> m_SphereHistories;
        std::size_t m_MaxHistorySize = 2000;
    };
}
