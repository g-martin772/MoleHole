#pragma once
#include <imgui_node_editor.h>
#include <vector>
#include <memory>

class AnimationGraph {
public:
    AnimationGraph();
    ~AnimationGraph();
    void Render();

private:
    std::unique_ptr<ax::NodeEditor::EditorContext, void(*)(ax::NodeEditor::EditorContext*)> m_Context;
    std::vector<ax::NodeEditor::NodeId> m_NodeIds;
    std::vector<ax::NodeEditor::PinId> m_InputPins;
    std::vector<ax::NodeEditor::PinId> m_OutputPins;
    std::vector<std::pair<ax::NodeEditor::LinkId, std::pair<ax::NodeEditor::PinId, ax::NodeEditor::PinId>>> m_Links;
    int m_NextId;
};

