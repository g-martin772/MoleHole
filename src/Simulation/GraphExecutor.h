#pragma once
#include "../Application/AnimationGraph.h"
#include "Simulation/Scene.h"
#include <unordered_map>
#include <variant>
#include <vector>
#include <glm/glm.hpp>

class Camera;

class GraphExecutor {
public:
    using Value = std::variant<std::monostate, bool, int, float, glm::vec2, glm::vec3, glm::vec4, std::string, BlackHole*, Camera*>;

    GraphExecutor(AnimationGraph* graph, Scene* scene);

    void ExecuteStartEvent();
    void ExecuteTickEvent(float deltaTime);

private:
    AnimationGraph* m_pGraph;
    Scene* m_pScene;
    std::unordered_map<std::string, Value> m_Variables;
    std::unordered_map<int, Value> m_PinValues;

    void ExecuteFlowFromPin(ax::NodeEditor::PinId pinId, float deltaTime = 0.0f);
    void ExecuteNode(AnimationGraph::Node* node, ax::NodeEditor::PinId entryPin, float deltaTime);

    Value EvaluatePinValue(ax::NodeEditor::PinId pinId, float deltaTime);
    Value EvaluateNode(AnimationGraph::Node* node, float deltaTime);

    AnimationGraph::Node* FindNodeByOutputPin(ax::NodeEditor::PinId pinId) const;
    AnimationGraph::Node* FindNodeByInputPin(ax::NodeEditor::PinId pinId) const;
    ax::NodeEditor::PinId GetConnectedOutputPin(ax::NodeEditor::PinId inputPinId) const;

    Value ExecuteMathOperation(AnimationGraph::Node* node, float deltaTime);
    Value ExecuteConstant(AnimationGraph::Node* node);
    Value ExecuteDecomposer(AnimationGraph::Node* node, float deltaTime);
    Value ExecuteSceneGetter(AnimationGraph::Node* node);
    void ExecuteSetter(AnimationGraph::Node* node, float deltaTime);
    void ExecuteControlFlow(AnimationGraph::Node* node, ax::NodeEditor::PinId entryPin, float deltaTime);
    void ExecutePrint(AnimationGraph::Node* node, float deltaTime);
    Value ExecuteVariableGet(AnimationGraph::Node* node);
    void ExecuteVariableSet(AnimationGraph::Node* node, float deltaTime);

    static std::string ValueToString(const Value& val);

    template<typename T>
    T GetValueAs(const Value& val, T defaultValue = T{});
};
