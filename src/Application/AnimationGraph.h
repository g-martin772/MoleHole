#pragma once
#include <imgui_node_editor.h>
#include <vector>
#include <memory>
#include <string>
#include <variant>
#include <random>

#include "glm/glm.hpp"
#include "yaml-cpp/emitter.h"
#include "yaml-cpp/node/node.h"

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
        Blackhole, Star, Camera, Object,

        // Variable
        VariableGet, VariableSet
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

        // For variable nodes - the variable name they reference
        std::string VariableName;
        // For scene object nodes - the index of the object in the scene
        int SceneObjectIndex = -1;
    };
    struct Link {
        ed::LinkId Id;
        ed::PinId StartPinId;
        ed::PinId EndPinId;
    };

    static bool ArePinsCompatible(PinType a, PinType b);
    static Node CreateEventNode(int id);
    static Node CreateTickEventNode(int id);
    static Node CreatePrintNode(int id);
    static Node CreateConstantNode(int id, const std::string& value);

    // Math Function Nodes
    static Node CreateMathNode(int id, NodeSubType subType, PinType valueType);

    // Constant Nodes
    static Node CreateConstantFloatNode(int id, float value = 0.0f);
    static Node CreateConstantVec2Node(int id, glm::vec2 value = glm::vec2(0.0f));
    static Node CreateConstantVec3Node(int id, glm::vec3 value = glm::vec3(0.0f));
    static Node CreateConstantVec4Node(int id, glm::vec4 value = glm::vec4(0.0f));
    static Node CreateConstantIntNode(int id, int value = 0);
    static Node CreateConstantBoolNode(int id, bool value = false);
    static Node CreateConstantStringNode(int id, const std::string& value = "");

    // Control Nodes
    static Node CreateIfNode(int id);
    static Node CreateForNode(int id);
    static Node CreateBranchNode(int id);

    // Decomposer Nodes
    static Node CreateBlackHoleDecomposerNode(int id);
    static Node CreateCameraDecomposerNode(int id);

    // Scene Access Nodes
    static Node CreateGetBlackHoleFromSceneNode(int id, int index = -1);
    static Node CreateGetCameraFromSceneNode(int id);

    // Setter Nodes
    static Node CreateBlackHoleSetterNode(int id);
    static Node CreateCameraSetterNode(int id);

    // Variable Nodes
    static Node CreateVariableGetNode(int id, PinType varType, const std::string& varName = "");
    static Node CreateVariableSetNode(int id, PinType varType, const std::string& varName = "");

    void Serialize(YAML::Emitter& out) const;
    void Deserialize(const YAML::Node& node);

    std::vector<std::string>& GetVariables() { return m_Variables; }
    std::vector<std::string>& GetSceneObjects() { return m_SceneObjects; }
private:
    std::unique_ptr<ed::EditorContext, void(*)(ed::EditorContext*)> m_Context;
    std::vector<Node> m_Nodes;
    std::vector<Link> m_Links;
    std::mt19937 m_RandomGenerator;
    std::vector<std::string> m_Variables;
    std::vector<std::string> m_SceneObjects;

    int GenerateRandomId();
    void ShowNodeCreationPopup();
};
