export module MoleHole:Simulation.GraphIr;

import std;
import :Simulation.AnimationGraph;
import :Simulation.SceneGraphs;
import :Simulation.GraphValidation;

export namespace MoleHole
{
    struct IrInput
    {
        int Source{-1};
        int SourcePin{-1};
        PinType Type{PinType::Float};
    };

    struct IrFlowEdge
    {
        int LinkId{0};
        int Target{-1};
        int TargetPin{0};
        bool Cut{false};
    };

    struct IrNode
    {
        int Id{0};
        NodeType Type{NodeType::Event};
        NodeSubType SubType{NodeSubType::None};
        Value Constant;
        std::string Variable;
        std::string Component;
        std::string Function;
        std::string Label;
        std::uint64_t TargetGuid{0};
        std::vector<IrInput> Inputs;
        std::vector<int> OutputPinIds;
        std::vector<PinType> OutputTypes;
        std::vector<std::vector<IrFlowEdge>> Flow;
        int DataConsumers{0};
        bool Pure{false};
        bool Stable{true};
    };

    struct IrGraph
    {
        std::string Name;
        bool IsFunction{false};
        bool IsComponent{false};
        FunctionSignature Signature;
        std::vector<IrNode> Nodes;
        std::vector<int> PureOrder;
        std::vector<int> StartEvents;
        std::vector<int> TickEvents;
        // Custom event, on-trigger and on-key definitions: roots that other flows or the host invoke by name.
        std::vector<int> EventHandlers;
        bool HasLatent{false};
        int Entry{-1};
        int Return{-1};
        std::vector<Variable> Variables;
        std::vector<Diagnostic> Diagnostics;
        std::uint64_t Hash{0};

        [[nodiscard]] bool HasErrors() const;
    };

    [[nodiscard]] std::uint64_t HashGraph(const NamedGraph& graph);
    [[nodiscard]] IrGraph CompileIr(const NamedGraph& graph, const SceneGraphs* scene = nullptr);

    // Compiled graphs are kept until the serialized graph changes.
    class IrCache
    {
    public:
        [[nodiscard]] std::shared_ptr<const IrGraph> Get(const NamedGraph& graph, const SceneGraphs* scene = nullptr);
        [[nodiscard]] std::size_t Compilations() const { return m_Compilations; }
        void Clear() { m_Entries.clear(); }

    private:
        std::unordered_map<std::string, std::shared_ptr<const IrGraph>> m_Entries;
        std::size_t m_Compilations{0};
    };
}
