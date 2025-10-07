#pragma once
#include "AnimationGraph.h"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <vector>
#include <string>

namespace ed = ax::NodeEditor;

class NodeBuilder {
public:
    explicit NodeBuilder(const AnimationGraph::Node &node, std::vector<AnimationGraph::Variable>& variables, std::vector<std::string>& sceneObjects);
    void DrawNode();

private:
    const AnimationGraph::Node &m_Node;
    std::vector<AnimationGraph::Variable>& m_Variables;
    std::vector<std::string>& m_SceneObjects;

    float CalculateNodeWidth();
    void DrawHeader(const ImVec4 &headerColor, float nodeWidth);
    void DrawNodeIcon() const;
    void DrawPinsAndContent(float nodeWidth);
    static void DrawInputPin(const AnimationGraph::Pin &pin);
    static void DrawOutputPin(const AnimationGraph::Pin &pin);
    void DrawConstantValueInput();
    void DrawVariableSelector();
    void DrawSceneObjectSelector();
};
