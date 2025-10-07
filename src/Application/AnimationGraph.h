#pragma once
#include <imgui_node_editor.h>
#include <vector>
#include <memory>
#include <string>
#include <variant>

#include "glm/glm.hpp"

namespace ed = ax::NodeEditor;

class AnimationGraph {
public:
    AnimationGraph();
    ~AnimationGraph();
    void Render();

    enum class PinType {
        Flow,
        Bool,
        F1, F2, F3, F4,
        I1, I2, I3, I4,
        RGB, RGBA, HSL, HSLA,
        String,
        BlackHole,
        Star,
        Object,
        Camera,
        Function
    };
    enum class NodeType {
        Event,
        Function,
        Variable,
        Constant,
        Decomposer,
        Setter,
        Control,
        Print,
        Other
    };
    enum class NodeSubType {
        None = 0,

        // Transformer
        Add, Sub, Mul, Div,
        Min, Max,
        Negate,
        Sin, Cos, Tan,
        Asin, Acos, Atan,
        Sqrt,
        Exp, Log,
        Lerp,
        Clamp,
        Round,
        Floor, Ceil,
        Sign,
        Length,
        Distance,
        Color,

        // Control
        And, Or,
        If, For, Branch,

        // Event
        Start, Tick, Collision,

        // Decomposer/Setter
        Blackhole, Star, Camera, Object
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
        NodeSubType SubType;
        std::vector<Pin> Inputs;
        std::vector<Pin> Outputs;
        std::variant<std::monostate, std::string, float, int, glm::vec2, glm::vec3, glm::vec4> Value;
    };
    struct Link {
        ed::LinkId Id;
        ed::PinId StartPinId;
        ed::PinId EndPinId;
    };

    static bool ArePinsCompatible(PinType a, PinType b);
    static Node CreateEventNode(int id);
    static Node CreatePrintNode(int id);
    static Node CreateConstantNode(int id, const std::string& value);


private:
    std::unique_ptr<ed::EditorContext, void(*)(ed::EditorContext*)> m_Context;
    std::vector<Node> m_Nodes;
    std::vector<Link> m_Links;
    int m_NextId;
};
