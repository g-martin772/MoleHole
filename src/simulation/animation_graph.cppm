module;
#include <yaml-cpp/yaml.h>
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
        Entity,
        Reroute,
        Call,
        Latent,
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
        LookAt,

        // Entity
        SpawnEntity, DestroyEntity, CloneEntity,

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

        // Functions: entry/return live inside a function graph, call nodes in any graph (named by Node::FunctionName)
        FunctionEntry, FunctionReturn, FunctionCall,

        // Latent nodes suspend the running flow (Luau runtime only)
        Delay, WaitUntil, WaitForEvent, Interpolate,

        // Flow control and events (Luau runtime only)
        Sequence, DoOnce, Gate, Switch, ForEach, While,
        CustomEvent, CustomEventCall, OnTrigger, OnKey,
    };

    constexpr int kPinIdStride = 1000;

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
        std::string FunctionName;
        // Event name, key name, component query or easing, depending on the node.
        std::string Label;
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
        Value Default;
    };

    struct FunctionParam
    {
        int Key{0};
        std::string Name;
        PinType Type{PinType::Float};
    };

    // A non-pure function also has one flow input and one flow output; Key keeps pin ids stable across edits.
    struct FunctionSignature
    {
        bool Pure{false};
        std::vector<FunctionParam> Inputs;
        std::vector<FunctionParam> Outputs;
        int NextKey{0};
    };

    struct Comment
    {
        int Id{0};
        std::string Title{"Comment"};
        glm::vec2 Position{0.0f, 0.0f};
        glm::vec2 Size{320.0f, 200.0f};
        glm::vec4 Color{0.25f, 0.45f, 0.75f, 0.35f};
    };

    class AnimationGraphData
    {
    public:
        std::vector<Node> Nodes;
        std::vector<Link> Links;
        std::vector<Variable> Variables;
        std::vector<Comment> Comments;
        int NextId{1};

        int AllocateId() { return NextId++; }

        [[nodiscard]] Node* FindNode(int nodeId);
        [[nodiscard]] const Node* FindNode(int nodeId) const;
        [[nodiscard]] Node* FindNodeByInputPin(int pinId);
        [[nodiscard]] const Node* FindNodeByInputPin(int pinId) const;
        [[nodiscard]] Node* FindNodeByOutputPin(int pinId);
        [[nodiscard]] const Node* FindNodeByOutputPin(int pinId) const;

        [[nodiscard]] Comment* FindComment(int commentId);

        void RemoveNode(int nodeId);
        void RemoveLink(int linkId);
    };

    [[nodiscard]] bool ArePinsCompatible(PinType a, PinType b);
    [[nodiscard]] Value DefaultValueFor(PinType type);

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

    [[nodiscard]] Node CreateRerouteNode(int id, PinType type);
    [[nodiscard]] Node CreateSpawnEntityNode(int id);
    [[nodiscard]] Node CreateDestroyEntityNode(int id);
    [[nodiscard]] Node CreateCloneEntityNode(int id);

    [[nodiscard]] Node CreateDelayNode(int id);
    [[nodiscard]] Node CreateWaitUntilNode(int id);
    [[nodiscard]] Node CreateWaitForEventNode(int id, const std::string& event = {});
    [[nodiscard]] Node CreateInterpolateNode(int id, PinType valueType, const std::string& easing = "smoothstep");
    [[nodiscard]] Node CreateSequenceNode(int id, int outputs);
    [[nodiscard]] Node CreateDoOnceNode(int id);
    [[nodiscard]] Node CreateGateNode(int id);
    [[nodiscard]] Node CreateSwitchNode(int id, int cases);
    [[nodiscard]] Node CreateForEachEntityNode(int id, const std::string& component = "Transform");
    [[nodiscard]] Node CreateWhileNode(int id);
    [[nodiscard]] Node CreateOnTriggerNode(int id);
    [[nodiscard]] Node CreateOnKeyNode(int id, const std::string& key = "Space");

    // Keys of custom event parameters index the pins: parameter k lives at pin index k + 1 (index 0 is the flow pin).
    struct EventParam
    {
        int Key{0};
        std::string Name;
        PinType Type{PinType::Float};
    };
    [[nodiscard]] Node CreateCustomEventNode(int id, const std::string& name, const std::vector<EventParam>& params = {});
    [[nodiscard]] Node CreateCallEventNode(int id, const std::string& name, const std::vector<EventParam>& params = {});

    // Nodes that only the Luau runtime can run; the interpreter reports an error when execution reaches them.
    [[nodiscard]] bool IsLuauOnly(NodeType type, NodeSubType subType);
    [[nodiscard]] bool IsLuauOnly(const Node& node);
    [[nodiscard]] bool IsLatentNode(const Node& node);
    // Event name a node registers or fires (empty for other nodes); on-trigger and on-key use reserved names.
    [[nodiscard]] std::string EventNameOf(const Node& node);
    constexpr const char* kTriggerEventName = "@trigger";
    constexpr const char* kKeyEventPrefix = "@key:";

    [[nodiscard]] YAML::Node GraphToNode(const AnimationGraphData& graph);
    [[nodiscard]] AnimationGraphData GraphFromNode(const YAML::Node& root);

    [[nodiscard]] std::string SerializeToYaml(const AnimationGraphData& graph);
    [[nodiscard]] AnimationGraphData DeserializeFromYaml(const std::string& yaml);

    [[nodiscard]] std::string ValueToString(const Value& value);

    [[nodiscard]] std::string PinTypeToText(PinType type);
    [[nodiscard]] PinType PinTypeFromText(const std::string& text);
    [[nodiscard]] YAML::Node ValueToYaml(const Value& value);
    [[nodiscard]] Value ValueFromYaml(const YAML::Node& node);
    [[nodiscard]] YAML::Node SignatureToNode(const FunctionSignature& signature);
    [[nodiscard]] FunctionSignature SignatureFromNode(const YAML::Node& node);

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
