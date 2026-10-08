export module MoleHole:Simulation.AnimationGraphExecutor;

import std;
import glm;
import GPP;
import :Simulation.AnimationGraph;
import :Simulation.AnimationGraphProperties;

export namespace MoleHole
{
    using PendingWrite = std::function<void(GPP::Scene&)>;
    using PendingWrites = std::vector<PendingWrite>;

    class GraphExecutor
    {
    public:
        explicit GraphExecutor(const AnimationGraphData& graph, std::function<void(std::string)> onPrint = nullptr);

        [[nodiscard]] PendingWrites ExecuteStartEvent(const GPP::Scene& scene);
        [[nodiscard]] PendingWrites ExecuteTickEvent(const GPP::Scene& scene, float deltaTime);

    private:
        struct ExecutionContext
        {
            const GPP::Scene& Scene;
            float DeltaTime{0.0f};
            PendingWrites Writes;
        };

        const AnimationGraphData& m_Graph;
        std::function<void(std::string)> m_OnPrint;
        std::unordered_map<std::string, Value> m_Variables;
        std::unordered_map<int, Value> m_PinValues;

        void ExecuteFlowFromPin(int pinId, ExecutionContext& ctx);
        void ExecuteNode(const Node* node, ExecutionContext& ctx);

        Value EvaluatePinValue(int pinId, ExecutionContext& ctx);
        Value EvaluateNode(const Node* node, ExecutionContext& ctx);

        Value ExecuteMathOperation(const Node* node, ExecutionContext& ctx);
        Value ExecuteConstant(const Node* node);
        void ExecuteDecomposer(const Node* node, ExecutionContext& ctx);
        Value ExecuteSceneGetter(const Node* node, ExecutionContext& ctx);
        void ExecuteSetter(const Node* node, ExecutionContext& ctx);
        void ExecuteControlFlow(const Node* node, ExecutionContext& ctx);
        void ExecutePrint(const Node* node, ExecutionContext& ctx);
        Value ExecuteVariableGet(const Node* node);
        void ExecuteVariableSet(const Node* node, ExecutionContext& ctx);
    };
}
