#pragma once
#include <imgui_node_editor.h>
#include <vector>
#include <memory>
#include <string>

namespace ed = ax::NodeEditor;

class AnimationGraph {
public:
    AnimationGraph();
    ~AnimationGraph();
    void Render();

    enum class PinType {
        Flow,
        Bool,
        Int,
        Float,
        String,
        Object,
        Function,
        Delegate
    };
    enum class NodeType {
        Event,
        Function,
        Variable,
        Math
    };
    struct Pin {
        ed::PinId Id;
        std::string Name;
        PinType Type;
        bool IsInput;
    };
    struct Node {
        ed::NodeId Id;
        std::string Name;
        NodeType Type;
        std::vector<Pin> Inputs;
        std::vector<Pin> Outputs;
    };
    struct Link {
        ed::LinkId Id;
        ed::PinId StartPinId;
        ed::PinId EndPinId;
    };


private:
    std::unique_ptr<ed::EditorContext, void(*)(ed::EditorContext*)> m_Context;
    std::vector<Node> m_Nodes;
    std::vector<Link> m_Links;
    int m_NextId;
};
