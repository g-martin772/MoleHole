export module MoleHole:Simulation.GraphHistory;

import std;
import :Simulation.AnimationGraph;

export namespace MoleHole
{
    // Snapshot-based undo/redo. Edits recorded with the same non-empty key while the entry is open merge into one step.
    class GraphHistory
    {
    public:
        explicit GraphHistory(const std::size_t limit = 128) : m_Limit(std::max<std::size_t>(limit, 2)) {}

        void Reset(const AnimationGraphData& graph)
        {
            m_States.assign(1, SerializeToYaml(graph));
            m_Index = 0;
            m_Open = false;
            m_Key.clear();
        }

        bool Record(const AnimationGraphData& graph, const std::string_view coalesceKey = {})
        {
            if (m_States.empty())
            {
                Reset(graph);
                return false;
            }
            auto snapshot = SerializeToYaml(graph);
            if (snapshot == m_States[m_Index]) return false;

            const bool merge = m_Open && !coalesceKey.empty() && coalesceKey == m_Key && m_Index > 0;
            if (merge)
            {
                m_States[m_Index] = std::move(snapshot);
                return true;
            }
            m_States.resize(m_Index + 1);
            m_States.push_back(std::move(snapshot));
            if (m_States.size() > m_Limit) m_States.erase(m_States.begin());
            m_Index = m_States.size() - 1;
            m_Key = std::string(coalesceKey);
            m_Open = !coalesceKey.empty();
            return true;
        }

        void Seal() { m_Open = false; }

        [[nodiscard]] bool CanUndo() const { return m_Index > 0; }
        [[nodiscard]] bool CanRedo() const { return m_Index + 1 < m_States.size(); }
        [[nodiscard]] std::size_t Size() const { return m_States.size(); }

        bool Undo(AnimationGraphData& graph)
        {
            if (!CanUndo()) return false;
            graph = DeserializeFromYaml(m_States[--m_Index]);
            m_Open = false;
            return true;
        }

        bool Redo(AnimationGraphData& graph)
        {
            if (!CanRedo()) return false;
            graph = DeserializeFromYaml(m_States[++m_Index]);
            m_Open = false;
            return true;
        }

    private:
        std::vector<std::string> m_States;
        std::size_t m_Index{0};
        std::size_t m_Limit;
        std::string m_Key;
        bool m_Open{false};
    };
}
