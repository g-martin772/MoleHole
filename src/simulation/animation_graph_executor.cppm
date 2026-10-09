export module MoleHole:Simulation.AnimationGraphExecutor;

import std;
import glm;
import GPP;
import :Simulation.AnimationGraph;
import :Simulation.AnimationGraphProperties;
import :Simulation.SceneGraphs;
import :Simulation.GraphFunctions;
import :Simulation.GraphTrace;

export namespace MoleHole
{
    using PendingWrite = std::function<void(GPP::Scene&)>;
    using PendingWrites = std::vector<PendingWrite>;

    class GraphExecutor
    {
    public:
        explicit GraphExecutor(const AnimationGraphData& graph, std::function<void(std::string)> onPrint = nullptr,
                               std::uint64_t selfGuid = 0);

        void SetGuidSource(std::function<std::uint64_t()> source) { m_GuidSource = std::move(source); }
        void SetFunctionLibrary(const FunctionLibrary* library) { m_Functions = library; }
        void SetTraceSink(ITraceSink* sink) { m_Trace = sink; }
        void SetName(std::string name) { m_Name = std::move(name); }
        [[nodiscard]] bool Aborted() const { return m_Aborted; }

        [[nodiscard]] PendingWrites ExecuteStartEvent(const GPP::Scene& scene);
        [[nodiscard]] PendingWrites ExecuteTickEvent(const GPP::Scene& scene, float deltaTime);

    private:
        struct ExecutionContext
        {
            const GPP::Scene& Scene;
            float DeltaTime{0.0f};
            PendingWrites Writes;
            std::unordered_set<std::uint64_t> Spawned;
        };

        const AnimationGraphData& m_Graph;
        std::function<void(std::string)> m_OnPrint;
        std::uint64_t m_SelfGuid{0};
        std::function<std::uint64_t()> m_GuidSource;
        std::unordered_map<std::string, Value> m_Variables;
        std::unordered_map<int, Value> m_PinValues;
        const FunctionLibrary* m_Functions{nullptr};
        std::vector<std::string> m_CallStack;
        std::vector<Value> m_ReturnValues;
        bool m_Returned{false};
        std::unordered_set<int> m_Evaluating;
        int m_FlowDepth{0};
        ITraceSink* m_Trace{nullptr};
        std::string m_Name;
        bool m_Aborted{false};

        void SetPin(int pinId, Value value);
        void Report(const Node* node, TraceSeverity severity, std::string message);
        [[nodiscard]] bool Enter(const Node* node);

        void ExecuteFlowFromPin(int pinId, ExecutionContext& ctx);
        void ExecuteNode(const Node* node, ExecutionContext& ctx);

        Value EvaluatePinValue(int pinId, ExecutionContext& ctx);
        Value EvaluateNode(const Node* node, ExecutionContext& ctx);

        Value ExecuteMathOperation(const Node* node, ExecutionContext& ctx);
        Value ExecuteConstant(const Node* node);
        void ExecuteDecomposer(const Node* node, ExecutionContext& ctx);
        Value ExecuteSceneGetter(const Node* node, ExecutionContext& ctx);
        void ExecuteSetter(const Node* node, ExecutionContext& ctx);
        void ExecuteEntityNode(const Node* node, ExecutionContext& ctx);
        [[nodiscard]] static bool EntityKnown(const ExecutionContext& ctx, std::uint64_t guid);
        void ExecuteControlFlow(const Node* node, ExecutionContext& ctx);
        void ExecutePrint(const Node* node, ExecutionContext& ctx);
        void ExecuteCall(const Node* node, ExecutionContext& ctx);
        std::vector<Value> RunFunction(const std::string& name, const FunctionDefinition& definition,
                                       const std::vector<Value>& args, ExecutionContext& ctx);
        Value ExecuteVariableGet(const Node* node);
        void ExecuteVariableSet(const Node* node, ExecutionContext& ctx);
    };

    class GraphSetExecutor
    {
    public:
        GraphSetExecutor(const SceneGraphs& graphs, std::function<void(std::string)> onPrint = nullptr);

        void SetGuidSource(const std::function<std::uint64_t()>& source);
        void SetTraceSink(ITraceSink* sink);

        [[nodiscard]] PendingWrites ExecuteStartEvent(const GPP::Scene& scene);
        [[nodiscard]] PendingWrites ExecuteTickEvent(const GPP::Scene& scene, float deltaTime);

    private:
        ITraceSink* m_Trace{nullptr};
        std::unique_ptr<FunctionLibrary> m_Functions;
        std::vector<std::unique_ptr<GraphExecutor>> m_Executors;
    };
}
