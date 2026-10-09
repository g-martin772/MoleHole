export module MoleHole:Simulation.AnimationGraph;

import std;
import glm;

export namespace MoleHole
{
    enum class PinType
    {
        Flow,
        Bool,
        Float,
        Int,
        Vec2,
        Vec3,
        Vec4,
        String,
        Object,
    };

    enum class NodeType
    {
        Event,
        Function,
        Variable,
        Constant,
        Decomposer,
        Setter,
        Getter,
        Control,
        Print,
    };

    enum class NodeSubType
    {
        None,

        // Function (math)
        Add, Sub, Mul, Div,
        Min, Max,
        Negate,
        Sin, Cos, Tan,
        Sqrt,
        Length,
        Distance,
        Lerp,
        Clamp,
        And, Or,

        // Control
        Branch,
        For,

        // Event
        Start, Tick,

        // Decomposer/Setter/Getter
        BlackHole,
        Sphere,
        Transform,

        // Variable
        VariableGet, VariableSet,

        // Decomposer/Setter/Getter for any registered component (named by Node::Component)
        Component,
    };

    using Value = std::variant<std::monostate, bool, int, float, glm::vec2, glm::vec3, glm::vec4,
                                std::string, std::uint64_t>;

    struct Pin
    {
        int Id{0};
        std::string Name;
        PinType Type{PinType::Flow};
        bool IsInput{false};
    };

    struct Node
    {
        int Id{0};
        std::string Name;
        NodeType Type{NodeType::Event};
        NodeSubType SubType{NodeSubType::None};
        std::vector<Pin> Inputs;
        std::vector<Pin> Outputs;
        Value ConstantValue;
        std::string VariableName;
        std::uint64_t TargetGuid{0};
        std::string Component;
        glm::vec2 Position{0.0f, 0.0f};
    };

    struct Link
    {
        int Id{0};
        int StartPinId{0};
        int EndPinId{0};
    };

    struct Variable
    {
        std::string Name;
        PinType Type{PinType::Float};
    };

    class AnimationGraphData
    {
    public:
        std::vector<Node> Nodes;
        std::vector<Link> Links;
        std::vector<Variable> Variables;
        int NextId{1};

        int AllocateId() { return NextId++; }

        [[nodiscard]] Node* FindNode(int nodeId);
        [[nodiscard]] const Node* FindNode(int nodeId) const;
        [[nodiscard]] Node* FindNodeByInputPin(int pinId);
        [[nodiscard]] const Node* FindNodeByInputPin(int pinId) const;
        [[nodiscard]] Node* FindNodeByOutputPin(int pinId);
        [[nodiscard]] const Node* FindNodeByOutputPin(int pinId) const;

        void RemoveNode(int nodeId);
        void RemoveLink(int linkId);
    };

    [[nodiscard]] bool ArePinsCompatible(PinType a, PinType b);

    [[nodiscard]] Node CreateStartEventNode(int id);
    [[nodiscard]] Node CreateTickEventNode(int id);
    [[nodiscard]] Node CreateMathNode(int id, NodeSubType op);
    [[nodiscard]] Node CreateConstantNode(int id, PinType type);
    [[nodiscard]] Node CreateBranchNode(int id);
    [[nodiscard]] Node CreateForNode(int id);
    [[nodiscard]] Node CreatePrintNode(int id);
    [[nodiscard]] Node CreateVariableGetNode(int id, const std::string& name, PinType type);
    [[nodiscard]] Node CreateVariableSetNode(int id, const std::string& name, PinType type);

    [[nodiscard]] Node CreateDecomposerNode(int id, NodeSubType category);
    [[nodiscard]] Node CreateSetterNode(int id, NodeSubType category);
    [[nodiscard]] Node CreateGetterNode(int id, NodeSubType category);

    [[nodiscard]] Node CreateDecomposerNode(int id, const std::string& component);
    [[nodiscard]] Node CreateSetterNode(int id, const std::string& component);
    [[nodiscard]] Node CreateGetterNode(int id, const std::string& component);

    [[nodiscard]] std::string SerializeToYaml(const AnimationGraphData& graph);
    [[nodiscard]] AnimationGraphData DeserializeFromYaml(const std::string& yaml);

    [[nodiscard]] std::string ValueToString(const Value& value);

    template <typename T>
    [[nodiscard]] T GetValueAs(const Value& value, T defaultValue = T{})
    {
        if (std::holds_alternative<T>(value))
        {
            return std::get<T>(value);
        }
        if constexpr (std::is_same_v<T, bool>)
        {
            if (std::holds_alternative<int>(value)) { return std::get<int>(value) != 0; }
            if (std::holds_alternative<float>(value)) { return std::get<float>(value) != 0.0f; }
        }
        if constexpr (std::is_same_v<T, float>)
        {
            if (std::holds_alternative<int>(value)) { return static_cast<float>(std::get<int>(value)); }
        }
        if constexpr (std::is_same_v<T, int>)
        {
            if (std::holds_alternative<float>(value)) { return static_cast<int>(std::get<float>(value)); }
        }
        return defaultValue;
    }
}
