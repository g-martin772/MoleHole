export module MoleHole:UI.GraphStyle;

import std;
import glm;
import :UI.WidgetLogic;
import :Simulation.AnimationGraph;

export namespace MoleHole
{
    [[nodiscard]] inline glm::vec4 GraphPinColor(const PinType type)
    {
        switch (type)
        {
        case PinType::Flow: return HexColor(0xf2f2f2);
        case PinType::Bool: return HexColor(0xa30000);
        case PinType::Float: return HexColor(0x9cff3a);
        case PinType::Int: return HexColor(0x1fe0c0);
        case PinType::Vec2: return HexColor(0xf5c83a);
        case PinType::Vec3: return HexColor(0xffa31a);
        case PinType::Vec4: return HexColor(0xff6d3a);
        case PinType::String: return HexColor(0xf000c8);
        case PinType::Object: return HexColor(0x00a2ff);
        }
        return HexColor(0xffffff);
    }

    [[nodiscard]] inline glm::vec4 GraphNodeColor(const NodeType type)
    {
        switch (type)
        {
        case NodeType::Event: return HexColor(0xb02a2a);
        case NodeType::Function: return HexColor(0x2f8f55);
        case NodeType::Variable: return HexColor(0x2a7fb0);
        case NodeType::Constant: return HexColor(0x8a7020);
        case NodeType::Decomposer: return HexColor(0xa84a4a);
        case NodeType::Setter: return HexColor(0x7a4aa8);
        case NodeType::Getter: return HexColor(0x2f8590);
        case NodeType::Control: return HexColor(0x6e6e78);
        case NodeType::Print: return HexColor(0x3a78a8);
        case NodeType::Entity: return HexColor(0xb4671f);
        case NodeType::Reroute: return HexColor(0x5a5a5a);
        }
        return HexColor(0x808080);
    }

    [[nodiscard]] inline float GraphLinkThickness(const PinType type) { return type == PinType::Flow ? 3.0f : 2.0f; }

    [[nodiscard]] inline const char* PinTypeName(const PinType type)
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
        return "Unknown";
    }
}
