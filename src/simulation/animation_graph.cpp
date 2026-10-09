module;
#include <yaml-cpp/yaml.h>
module MoleHole;

import :Simulation.AnimationGraph;
import :Simulation.AnimationGraphProperties;
import std;
import glm;

namespace MoleHole
{
    namespace
    {
        std::string PinTypeToString(const PinType type)
        {
            switch (type)
            {
            case PinType::Flow: return "Flow";
            case PinType::Bool: return "Bool";
            case PinType::Float: return "Float";
            case PinType::Int: return "Int";
            case PinType::Vec2: return "Vec2";
            case PinType::Vec3: return "Vec3";
            case PinType::Vec4: return "Vec4";
            case PinType::String: return "String";
            case PinType::Object: return "Object";
            }
            return "Float";
        }

        PinType PinTypeFromString(const std::string& text)
        {
            if (text == "Flow") return PinType::Flow;
            if (text == "Bool") return PinType::Bool;
            if (text == "Int") return PinType::Int;
            if (text == "Vec2") return PinType::Vec2;
            if (text == "Vec3") return PinType::Vec3;
            if (text == "Vec4") return PinType::Vec4;
            if (text == "String") return PinType::String;
            if (text == "Object") return PinType::Object;
            return PinType::Float;
        }

        std::string NodeTypeToString(const NodeType type)
        {
            switch (type)
            {
            case NodeType::Event: return "Event";
            case NodeType::Function: return "Function";
            case NodeType::Variable: return "Variable";
            case NodeType::Constant: return "Constant";
            case NodeType::Decomposer: return "Decomposer";
            case NodeType::Setter: return "Setter";
            case NodeType::Getter: return "Getter";
            case NodeType::Control: return "Control";
            case NodeType::Print: return "Print";
            case NodeType::Entity: return "Entity";
            }
            return "Event";
        }

        NodeType NodeTypeFromString(const std::string& text)
        {
            if (text == "Function") return NodeType::Function;
            if (text == "Variable") return NodeType::Variable;
            if (text == "Constant") return NodeType::Constant;
            if (text == "Decomposer") return NodeType::Decomposer;
            if (text == "Setter") return NodeType::Setter;
            if (text == "Getter") return NodeType::Getter;
            if (text == "Control") return NodeType::Control;
            if (text == "Print") return NodeType::Print;
            if (text == "Entity") return NodeType::Entity;
            return NodeType::Event;
        }

        std::string NodeSubTypeToString(const NodeSubType type)
        {
            switch (type)
            {
            case NodeSubType::None: return "None";
            case NodeSubType::Add: return "Add";
            case NodeSubType::Sub: return "Sub";
            case NodeSubType::Mul: return "Mul";
            case NodeSubType::Div: return "Div";
            case NodeSubType::Min: return "Min";
            case NodeSubType::Max: return "Max";
            case NodeSubType::Negate: return "Negate";
            case NodeSubType::Sin: return "Sin";
            case NodeSubType::Cos: return "Cos";
            case NodeSubType::Tan: return "Tan";
            case NodeSubType::Sqrt: return "Sqrt";
            case NodeSubType::Length: return "Length";
            case NodeSubType::Distance: return "Distance";
            case NodeSubType::Lerp: return "Lerp";
            case NodeSubType::Clamp: return "Clamp";
            case NodeSubType::And: return "And";
            case NodeSubType::Or: return "Or";
            case NodeSubType::LookAt: return "LookAt";
            case NodeSubType::SpawnEntity: return "SpawnEntity";
            case NodeSubType::DestroyEntity: return "DestroyEntity";
            case NodeSubType::CloneEntity: return "CloneEntity";
            case NodeSubType::Branch: return "Branch";
            case NodeSubType::For: return "For";
            case NodeSubType::Start: return "Start";
            case NodeSubType::Tick: return "Tick";
            case NodeSubType::BlackHole: return "BlackHole";
            case NodeSubType::Sphere: return "Sphere";
            case NodeSubType::Transform: return "Transform";
            case NodeSubType::VariableGet: return "VariableGet";
            case NodeSubType::VariableSet: return "VariableSet";
            case NodeSubType::Component: return "Component";
            }
            return "None";
        }

        NodeSubType NodeSubTypeFromString(const std::string& text)
        {
            if (text == "Add") return NodeSubType::Add;
            if (text == "Sub") return NodeSubType::Sub;
            if (text == "Mul") return NodeSubType::Mul;
            if (text == "Div") return NodeSubType::Div;
            if (text == "Min") return NodeSubType::Min;
            if (text == "Max") return NodeSubType::Max;
            if (text == "Negate") return NodeSubType::Negate;
            if (text == "Sin") return NodeSubType::Sin;
            if (text == "Cos") return NodeSubType::Cos;
            if (text == "Tan") return NodeSubType::Tan;
            if (text == "Sqrt") return NodeSubType::Sqrt;
            if (text == "Length") return NodeSubType::Length;
            if (text == "Distance") return NodeSubType::Distance;
            if (text == "Lerp") return NodeSubType::Lerp;
            if (text == "Clamp") return NodeSubType::Clamp;
            if (text == "And") return NodeSubType::And;
            if (text == "Or") return NodeSubType::Or;
            if (text == "LookAt") return NodeSubType::LookAt;
            if (text == "SpawnEntity") return NodeSubType::SpawnEntity;
            if (text == "DestroyEntity") return NodeSubType::DestroyEntity;
            if (text == "CloneEntity") return NodeSubType::CloneEntity;
            if (text == "Branch") return NodeSubType::Branch;
            if (text == "For") return NodeSubType::For;
            if (text == "Start") return NodeSubType::Start;
            if (text == "Tick") return NodeSubType::Tick;
            if (text == "BlackHole") return NodeSubType::BlackHole;
            if (text == "Sphere") return NodeSubType::Sphere;
            if (text == "Transform") return NodeSubType::Transform;
            if (text == "VariableGet") return NodeSubType::VariableGet;
            if (text == "VariableSet") return NodeSubType::VariableSet;
            if (text == "Component") return NodeSubType::Component;
            return NodeSubType::None;
        }

        constexpr int kPinIdOutputOffset = 500;

        int InputPinId(const int nodeId, const int index) { return nodeId * kPinIdStride + 1 + index; }
        int OutputPinId(const int nodeId, const int index) { return nodeId * kPinIdStride + kPinIdOutputOffset + index; }

        Value DefaultValueForType(const PinType type)
        {
            switch (type)
            {
            case PinType::Bool: return false;
            case PinType::Int: return 0;
            case PinType::Float: return 0.0f;
            case PinType::Vec2: return glm::vec2(0.0f);
            case PinType::Vec3: return glm::vec3(0.0f);
            case PinType::Vec4: return glm::vec4(0.0f);
            case PinType::String: return std::string{};
            case PinType::Object: return std::uint64_t{0};
            case PinType::Flow: default: return std::monostate{};
            }
        }

        enum class MathArity { Unary, Binary, Ternary };

        MathArity ArityOf(const NodeSubType op)
        {
            switch (op)
            {
            case NodeSubType::Negate:
            case NodeSubType::Sin:
            case NodeSubType::Cos:
            case NodeSubType::Tan:
            case NodeSubType::Sqrt:
            case NodeSubType::Length:
                return MathArity::Unary;
            case NodeSubType::Lerp:
            case NodeSubType::Clamp:
                return MathArity::Ternary;
            default:
                return MathArity::Binary;
            }
        }

        bool IsBooleanOp(const NodeSubType op)
        {
            return op == NodeSubType::And || op == NodeSubType::Or;
        }

        YAML::Node EncodeValue(const Value& value)
        {
            YAML::Node node;
            std::visit([&node](auto&& v)
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                {
                    node["T"] = "None";
                }
                else if constexpr (std::is_same_v<T, bool>)
                {
                    node["T"] = "Bool";
                    node["V"] = v;
                }
                else if constexpr (std::is_same_v<T, int>)
                {
                    node["T"] = "Int";
                    node["V"] = v;
                }
                else if constexpr (std::is_same_v<T, float>)
                {
                    node["T"] = "Float";
                    node["V"] = v;
                }
                else if constexpr (std::is_same_v<T, glm::vec2>)
                {
                    node["T"] = "Vec2";
                    YAML::Node vn;
                    vn.push_back(v.x);
                    vn.push_back(v.y);
                    vn.SetStyle(YAML::EmitterStyle::Flow);
                    node["V"] = vn;
                }
                else if constexpr (std::is_same_v<T, glm::vec3>)
                {
                    node["T"] = "Vec3";
                    YAML::Node vn;
                    vn.push_back(v.x);
                    vn.push_back(v.y);
                    vn.push_back(v.z);
                    vn.SetStyle(YAML::EmitterStyle::Flow);
                    node["V"] = vn;
                }
                else if constexpr (std::is_same_v<T, glm::vec4>)
                {
                    node["T"] = "Vec4";
                    YAML::Node vn;
                    vn.push_back(v.x);
                    vn.push_back(v.y);
                    vn.push_back(v.z);
                    vn.push_back(v.w);
                    vn.SetStyle(YAML::EmitterStyle::Flow);
                    node["V"] = vn;
                }
                else if constexpr (std::is_same_v<T, std::string>)
                {
                    node["T"] = "String";
                    node["V"] = v;
                }
                else if constexpr (std::is_same_v<T, std::uint64_t>)
                {
                    node["T"] = "Object";
                    node["V"] = v;
                }
            }, value);
            return node;
        }

        Value DecodeValue(const YAML::Node& node)
        {
            if (!node || !node["T"]) { return std::monostate{}; }
            const auto tag = node["T"].as<std::string>();
            if (tag == "Bool") { return node["V"].as<bool>(); }
            if (tag == "Int") { return node["V"].as<int>(); }
            if (tag == "Float") { return node["V"].as<float>(); }
            if (tag == "Vec2")
            {
                const auto v = node["V"];
                return glm::vec2(v[0].as<float>(), v[1].as<float>());
            }
            if (tag == "Vec3")
            {
                const auto v = node["V"];
                return glm::vec3(v[0].as<float>(), v[1].as<float>(), v[2].as<float>());
            }
            if (tag == "Vec4")
            {
                const auto v = node["V"];
                return glm::vec4(v[0].as<float>(), v[1].as<float>(), v[2].as<float>(), v[3].as<float>());
            }
            if (tag == "String") { return node["V"].as<std::string>(); }
            if (tag == "Object") { return node["V"].as<std::uint64_t>(); }
            return std::monostate{};
        }

        YAML::Node EncodePin(const Pin& pin)
        {
            YAML::Node node;
            node["Id"] = pin.Id;
            node["Name"] = pin.Name;
            node["Type"] = PinTypeToString(pin.Type);
            node["IsInput"] = pin.IsInput;
            return node;
        }

        Pin DecodePin(const YAML::Node& node)
        {
            Pin pin;
            pin.Id = node["Id"] ? node["Id"].as<int>() : 0;
            pin.Name = node["Name"] ? node["Name"].as<std::string>() : std::string{};
            pin.Type = node["Type"] ? PinTypeFromString(node["Type"].as<std::string>()) : PinType::Float;
            pin.IsInput = node["IsInput"] ? node["IsInput"].as<bool>() : false;
            return pin;
        }
    }

    // ---- AnimationGraphData ------------------------------------------------------------------

    Node* AnimationGraphData::FindNode(const int nodeId)
    {
        for (auto& node : Nodes) { if (node.Id == nodeId) { return &node; } }
        return nullptr;
    }

    const Node* AnimationGraphData::FindNode(const int nodeId) const
    {
        for (const auto& node : Nodes) { if (node.Id == nodeId) { return &node; } }
        return nullptr;
    }

    Node* AnimationGraphData::FindNodeByInputPin(const int pinId)
    {
        for (auto& node : Nodes)
        {
            for (const auto& pin : node.Inputs) { if (pin.Id == pinId) { return &node; } }
        }
        return nullptr;
    }

    const Node* AnimationGraphData::FindNodeByInputPin(const int pinId) const
    {
        for (const auto& node : Nodes)
        {
            for (const auto& pin : node.Inputs) { if (pin.Id == pinId) { return &node; } }
        }
        return nullptr;
    }

    Node* AnimationGraphData::FindNodeByOutputPin(const int pinId)
    {
        for (auto& node : Nodes)
        {
            for (const auto& pin : node.Outputs) { if (pin.Id == pinId) { return &node; } }
        }
        return nullptr;
    }

    const Node* AnimationGraphData::FindNodeByOutputPin(const int pinId) const
    {
        for (const auto& node : Nodes)
        {
            for (const auto& pin : node.Outputs) { if (pin.Id == pinId) { return &node; } }
        }
        return nullptr;
    }

    void AnimationGraphData::RemoveNode(const int nodeId)
    {
        const Node* node = FindNode(nodeId);
        if (!node) { return; }

        std::vector<int> pinIds;
        for (const auto& pin : node->Inputs) { pinIds.push_back(pin.Id); }
        for (const auto& pin : node->Outputs) { pinIds.push_back(pin.Id); }

        std::erase_if(Links, [&pinIds](const Link& link)
        {
            return std::ranges::contains(pinIds, link.StartPinId) || std::ranges::contains(pinIds, link.EndPinId);
        });
        std::erase_if(Nodes, [nodeId](const Node& n) { return n.Id == nodeId; });
    }

    void AnimationGraphData::RemoveLink(const int linkId)
    {
        std::erase_if(Links, [linkId](const Link& link) { return link.Id == linkId; });
    }

    // ---- ArePinsCompatible --------------------------------------------------------------------

    bool ArePinsCompatible(const PinType a, const PinType b)
    {
        if (a == b) { return true; }
        if ((a == PinType::Int && b == PinType::Float) || (a == PinType::Float && b == PinType::Int))
        {
            return true;
        }
        return false;
    }

    // ---- Node factories ------------------------------------------------------------------------

    Node CreateStartEventNode(const int id)
    {
        Node node;
        node.Id = id;
        node.Name = "Start";
        node.Type = NodeType::Event;
        node.SubType = NodeSubType::Start;
        node.Outputs = { Pin{OutputPinId(id, 0), "Out", PinType::Flow, false} };
        return node;
    }

    Node CreateTickEventNode(const int id)
    {
        Node node;
        node.Id = id;
        node.Name = "Tick";
        node.Type = NodeType::Event;
        node.SubType = NodeSubType::Tick;
        node.Outputs = {
            Pin{OutputPinId(id, 0), "Out", PinType::Flow, false},
            Pin{OutputPinId(id, 1), "DeltaTime", PinType::Float, false},
        };
        return node;
    }

    Node CreateMathNode(const int id, const NodeSubType op)
    {
        Node node;
        node.Id = id;
        node.Name = NodeSubTypeToString(op);
        node.Type = NodeType::Function;
        node.SubType = op;

        if (op == NodeSubType::LookAt)
        {
            node.Name = "Look At";
            node.Inputs = {
                Pin{InputPinId(id, 0), "From", PinType::Vec3, true},
                Pin{InputPinId(id, 1), "Target", PinType::Vec3, true},
            };
            node.Outputs = { Pin{OutputPinId(id, 0), "Rotation", PinType::Vec3, false} };
            return node;
        }

        const PinType pinType = IsBooleanOp(op) ? PinType::Bool : PinType::Float;

        switch (ArityOf(op))
        {
        case MathArity::Unary:
            node.Inputs = { Pin{InputPinId(id, 0), "A", pinType, true} };
            break;
        case MathArity::Binary:
            node.Inputs = {
                Pin{InputPinId(id, 0), "A", pinType, true},
                Pin{InputPinId(id, 1), "B", pinType, true},
            };
            break;
        case MathArity::Ternary:
            if (op == NodeSubType::Lerp)
            {
                node.Inputs = {
                    Pin{InputPinId(id, 0), "A", pinType, true},
                    Pin{InputPinId(id, 1), "B", pinType, true},
                    Pin{InputPinId(id, 2), "T", PinType::Float, true},
                };
            }
            else // Clamp
            {
                node.Inputs = {
                    Pin{InputPinId(id, 0), "Value", pinType, true},
                    Pin{InputPinId(id, 1), "Min", pinType, true},
                    Pin{InputPinId(id, 2), "Max", pinType, true},
                };
            }
            break;
        }

        node.Outputs = { Pin{OutputPinId(id, 0), "Result", pinType, false} };
        return node;
    }

    Node CreateConstantNode(const int id, const PinType type)
    {
        Node node;
        node.Id = id;
        node.Name = "Constant " + PinTypeToString(type);
        node.Type = NodeType::Constant;
        node.SubType = NodeSubType::None;
        node.ConstantValue = DefaultValueForType(type);
        node.Outputs = { Pin{OutputPinId(id, 0), "Value", type, false} };
        return node;
    }

    Node CreateBranchNode(const int id)
    {
        Node node;
        node.Id = id;
        node.Name = "Branch";
        node.Type = NodeType::Control;
        node.SubType = NodeSubType::Branch;
        node.Inputs = {
            Pin{InputPinId(id, 0), "In", PinType::Flow, true},
            Pin{InputPinId(id, 1), "Condition", PinType::Bool, true},
        };
        node.Outputs = {
            Pin{OutputPinId(id, 0), "True", PinType::Flow, false},
            Pin{OutputPinId(id, 1), "False", PinType::Flow, false},
        };
        return node;
    }

    Node CreateForNode(const int id)
    {
        Node node;
        node.Id = id;
        node.Name = "For";
        node.Type = NodeType::Control;
        node.SubType = NodeSubType::For;
        node.Inputs = {
            Pin{InputPinId(id, 0), "In", PinType::Flow, true},
            Pin{InputPinId(id, 1), "Start", PinType::Int, true},
            Pin{InputPinId(id, 2), "End", PinType::Int, true},
        };
        node.Outputs = {
            Pin{OutputPinId(id, 0), "Body", PinType::Flow, false},
            Pin{OutputPinId(id, 1), "Index", PinType::Int, false},
            Pin{OutputPinId(id, 2), "Completed", PinType::Flow, false},
        };
        return node;
    }

    Node CreatePrintNode(const int id)
    {
        Node node;
        node.Id = id;
        node.Name = "Print";
        node.Type = NodeType::Print;
        node.SubType = NodeSubType::None;
        node.Inputs = {
            Pin{InputPinId(id, 0), "In", PinType::Flow, true},
            Pin{InputPinId(id, 1), "Value", PinType::String, true},
        };
        node.Outputs = { Pin{OutputPinId(id, 0), "Out", PinType::Flow, false} };
        return node;
    }

    Node CreateVariableGetNode(const int id, const std::string& name, const PinType type)
    {
        Node node;
        node.Id = id;
        node.Name = "Get " + name;
        node.Type = NodeType::Variable;
        node.SubType = NodeSubType::VariableGet;
        node.VariableName = name;
        node.Outputs = { Pin{OutputPinId(id, 0), name, type, false} };
        return node;
    }

    Node CreateVariableSetNode(const int id, const std::string& name, const PinType type)
    {
        Node node;
        node.Id = id;
        node.Name = "Set " + name;
        node.Type = NodeType::Variable;
        node.SubType = NodeSubType::VariableSet;
        node.VariableName = name;
        node.Inputs = {
            Pin{InputPinId(id, 0), "In", PinType::Flow, true},
            Pin{InputPinId(id, 1), name, type, true},
        };
        node.Outputs = { Pin{OutputPinId(id, 0), "Out", PinType::Flow, false} };
        return node;
    }

    namespace
    {
        NodeSubType SubTypeForComponent(const std::string& component)
        {
            const auto legacy = NodeSubTypeFromString(component);
            const bool isLegacyComponent = legacy == NodeSubType::BlackHole || legacy == NodeSubType::Sphere
                                           || legacy == NodeSubType::Transform;
            return isLegacyComponent ? legacy : NodeSubType::Component;
        }

        void InitComponentNode(Node& node, const int id, const NodeType type, const std::string& component)
        {
            node.Id = id;
            node.Type = type;
            node.SubType = SubTypeForComponent(component);
            node.Component = component;
        }
    }

    Node CreateDecomposerNode(const int id, const NodeSubType category)
    {
        return CreateDecomposerNode(id, NodeSubTypeToString(category));
    }

    Node CreateSetterNode(const int id, const NodeSubType category)
    {
        return CreateSetterNode(id, NodeSubTypeToString(category));
    }

    Node CreateGetterNode(const int id, const NodeSubType category)
    {
        return CreateGetterNode(id, NodeSubTypeToString(category));
    }

    Node CreateDecomposerNode(const int id, const std::string& component)
    {
        Node node;
        InitComponentNode(node, id, NodeType::Decomposer, component);
        node.Inputs = { Pin{InputPinId(id, 0), "Entity", PinType::Object, true} };

        if (const auto* propertyCategory = FindPropertyCategory(component))
        {
            node.Name = "Decompose " + propertyCategory->DisplayName;
            for (std::size_t i = 0; i < propertyCategory->Properties.size(); ++i)
            {
                const auto& property = propertyCategory->Properties[i];
                node.Outputs.push_back(Pin{OutputPinId(id, static_cast<int>(i)), property.Label, property.Type, false});
            }
        }
        else
        {
            node.Name = "Decompose";
        }
        return node;
    }

    Node CreateSetterNode(const int id, const std::string& component)
    {
        Node node;
        InitComponentNode(node, id, NodeType::Setter, component);
        node.Inputs.push_back(Pin{InputPinId(id, 0), "In", PinType::Flow, true});
        node.Inputs.push_back(Pin{InputPinId(id, 1), "Entity", PinType::Object, true});

        if (const auto* propertyCategory = FindPropertyCategory(component))
        {
            node.Name = "Set " + propertyCategory->DisplayName;
            for (std::size_t i = 0; i < propertyCategory->Properties.size(); ++i)
            {
                const auto& property = propertyCategory->Properties[i];
                node.Inputs.push_back(Pin{InputPinId(id, static_cast<int>(i) + 2), property.Label, property.Type, true});
            }
        }
        else
        {
            node.Name = "Set";
        }

        node.Outputs = {
            Pin{OutputPinId(id, 0), "Out", PinType::Flow, false},
            Pin{OutputPinId(id, 1), "Entity", PinType::Object, false}, // pass-through, mirrors legacy's chainable setter output
        };
        return node;
    }

    Node CreateGetterNode(const int id, const std::string& component)
    {
        Node node;
        InitComponentNode(node, id, NodeType::Getter, component);
        node.TargetGuid = 0;

        if (const auto* propertyCategory = FindPropertyCategory(component))
        {
            node.Name = "Get " + propertyCategory->DisplayName;
        }
        else
        {
            node.Name = "Get Entity";
        }

        node.Outputs = { Pin{OutputPinId(id, 0), "Entity", PinType::Object, false} };
        return node;
    }

    namespace
    {
        Node MakeEntityNode(const int id, const NodeSubType subType, std::string name)
        {
            Node node;
            node.Id = id;
            node.Name = std::move(name);
            node.Type = NodeType::Entity;
            node.SubType = subType;
            node.Inputs.push_back(Pin{InputPinId(id, 0), "In", PinType::Flow, true});
            node.Outputs.push_back(Pin{OutputPinId(id, 0), "Out", PinType::Flow, false});
            return node;
        }
    }

    Node CreateSpawnEntityNode(const int id)
    {
        Node node = MakeEntityNode(id, NodeSubType::SpawnEntity, "Spawn Entity");
        node.Inputs.push_back(Pin{InputPinId(id, 1), "Preset", PinType::String, true});
        node.Inputs.push_back(Pin{InputPinId(id, 2), "Position", PinType::Vec3, true});
        node.Inputs.push_back(Pin{InputPinId(id, 3), "Name", PinType::String, true});
        node.Outputs.push_back(Pin{OutputPinId(id, 1), "Entity", PinType::Object, false});
        return node;
    }

    Node CreateDestroyEntityNode(const int id)
    {
        Node node = MakeEntityNode(id, NodeSubType::DestroyEntity, "Destroy Entity");
        node.Inputs.push_back(Pin{InputPinId(id, 1), "Entity", PinType::Object, true});
        return node;
    }

    Node CreateCloneEntityNode(const int id)
    {
        Node node = MakeEntityNode(id, NodeSubType::CloneEntity, "Clone Entity");
        node.Inputs.push_back(Pin{InputPinId(id, 1), "Source", PinType::Object, true});
        node.Inputs.push_back(Pin{InputPinId(id, 2), "Position", PinType::Vec3, true});
        node.Outputs.push_back(Pin{OutputPinId(id, 1), "Entity", PinType::Object, false});
        return node;
    }

    // ---- Serialization --------------------------------------------------------------------------

    YAML::Node GraphToNode(const AnimationGraphData& graph)
    {
        YAML::Node root;
        root["NextId"] = graph.NextId;

        YAML::Node variablesNode(YAML::NodeType::Sequence);
        for (const auto& variable : graph.Variables)
        {
            YAML::Node v;
            v["Name"] = variable.Name;
            v["Type"] = PinTypeToString(variable.Type);
            variablesNode.push_back(v);
        }
        root["Variables"] = variablesNode;

        YAML::Node nodesNode(YAML::NodeType::Sequence);
        for (const auto& node : graph.Nodes)
        {
            YAML::Node n;
            n["Id"] = node.Id;
            n["Name"] = node.Name;
            n["Type"] = NodeTypeToString(node.Type);
            n["SubType"] = NodeSubTypeToString(node.SubType);
            n["VariableName"] = node.VariableName;
            n["TargetGuid"] = node.TargetGuid;
            if (node.SubType == NodeSubType::Component) { n["Component"] = node.Component; }
            n["ConstantValue"] = EncodeValue(node.ConstantValue);

            YAML::Node positionNode;
            positionNode.push_back(node.Position.x);
            positionNode.push_back(node.Position.y);
            positionNode.SetStyle(YAML::EmitterStyle::Flow);
            n["Position"] = positionNode;

            YAML::Node inputsNode(YAML::NodeType::Sequence);
            for (const auto& pin : node.Inputs) { inputsNode.push_back(EncodePin(pin)); }
            n["Inputs"] = inputsNode;

            YAML::Node outputsNode(YAML::NodeType::Sequence);
            for (const auto& pin : node.Outputs) { outputsNode.push_back(EncodePin(pin)); }
            n["Outputs"] = outputsNode;

            nodesNode.push_back(n);
        }
        root["Nodes"] = nodesNode;

        YAML::Node linksNode(YAML::NodeType::Sequence);
        for (const auto& link : graph.Links)
        {
            YAML::Node l;
            l["Id"] = link.Id;
            l["StartPinId"] = link.StartPinId;
            l["EndPinId"] = link.EndPinId;
            linksNode.push_back(l);
        }
        root["Links"] = linksNode;
        return root;
    }

    std::string SerializeToYaml(const AnimationGraphData& graph)
    {
        std::ostringstream stream;
        stream << GraphToNode(graph);
        return stream.str();
    }

    AnimationGraphData DeserializeFromYaml(const std::string& yaml)
    {
        return GraphFromNode(YAML::Load(yaml));
    }

    AnimationGraphData GraphFromNode(const YAML::Node& root)
    {
        AnimationGraphData graph;

        if (root["NextId"]) { graph.NextId = root["NextId"].as<int>(); }

        if (const auto variables = root["Variables"]; variables && variables.IsSequence())
        {
            for (const auto& v : variables)
            {
                Variable variable;
                variable.Name = v["Name"] ? v["Name"].as<std::string>() : std::string{};
                variable.Type = v["Type"] ? PinTypeFromString(v["Type"].as<std::string>()) : PinType::Float;
                graph.Variables.push_back(std::move(variable));
            }
        }

        if (const auto nodes = root["Nodes"]; nodes && nodes.IsSequence())
        {
            for (const auto& n : nodes)
            {
                Node node;
                node.Id = n["Id"] ? n["Id"].as<int>() : 0;
                node.Name = n["Name"] ? n["Name"].as<std::string>() : std::string{};
                node.Type = n["Type"] ? NodeTypeFromString(n["Type"].as<std::string>()) : NodeType::Event;
                node.SubType = n["SubType"] ? NodeSubTypeFromString(n["SubType"].as<std::string>()) : NodeSubType::None;
                node.VariableName = n["VariableName"] ? n["VariableName"].as<std::string>() : std::string{};
                node.TargetGuid = n["TargetGuid"] ? n["TargetGuid"].as<std::uint64_t>() : std::uint64_t{0};
                node.Component = n["Component"] ? n["Component"].as<std::string>() : NodeSubTypeToString(node.SubType);
                if (n["ConstantValue"]) { node.ConstantValue = DecodeValue(n["ConstantValue"]); }
                if (const auto position = n["Position"]; position && position.IsSequence() && position.size() >= 2)
                {
                    node.Position = glm::vec2(position[0].as<float>(), position[1].as<float>());
                }

                if (const auto inputs = n["Inputs"]; inputs && inputs.IsSequence())
                {
                    for (const auto& p : inputs) { node.Inputs.push_back(DecodePin(p)); }
                }
                if (const auto outputs = n["Outputs"]; outputs && outputs.IsSequence())
                {
                    for (const auto& p : outputs) { node.Outputs.push_back(DecodePin(p)); }
                }

                graph.Nodes.push_back(std::move(node));
            }
        }

        if (const auto links = root["Links"]; links && links.IsSequence())
        {
            for (const auto& l : links)
            {
                Link link;
                link.Id = l["Id"] ? l["Id"].as<int>() : 0;
                link.StartPinId = l["StartPinId"] ? l["StartPinId"].as<int>() : 0;
                link.EndPinId = l["EndPinId"] ? l["EndPinId"].as<int>() : 0;
                graph.Links.push_back(link);
            }
        }

        return graph;
    }

    std::string ValueToString(const Value& value)
    {
        if (std::holds_alternative<std::string>(value)) { return std::get<std::string>(value); }
        if (std::holds_alternative<bool>(value)) { return std::get<bool>(value) ? "true" : "false"; }
        if (std::holds_alternative<int>(value)) { return std::to_string(std::get<int>(value)); }
        if (std::holds_alternative<float>(value)) { return std::to_string(std::get<float>(value)); }
        if (std::holds_alternative<std::uint64_t>(value)) { return std::to_string(std::get<std::uint64_t>(value)); }
        if (std::holds_alternative<glm::vec2>(value))
        {
            const auto v = std::get<glm::vec2>(value);
            return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
        }
        if (std::holds_alternative<glm::vec3>(value))
        {
            const auto v = std::get<glm::vec3>(value);
            return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
        }
        if (std::holds_alternative<glm::vec4>(value))
        {
            const auto v = std::get<glm::vec4>(value);
            return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ", " +
                   std::to_string(v.w) + ")";
        }
        return "<empty>";
    }
}
