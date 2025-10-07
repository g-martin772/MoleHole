#define IMGUI_DEFINE_MATH_OPERATORS
#include "AnimationGraph.h"
#include "NodeBuilder.h"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <algorithm>
#include <yaml-cpp/yaml.h>
#include <random>

namespace ed = ax::NodeEditor;
using ax::NodeEditor::PinId;
using ax::NodeEditor::NodeId;

// Node Type Colors
constexpr auto EVENT_COLOR = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
constexpr auto FUNCTION_COLOR = ImVec4(0.18f, 0.8f, 0.44f, 1.0f);
constexpr auto VARIABLE_COLOR = ImVec4(0.95f, 0.77f, 0.06f, 1.0f);
constexpr auto CONSTANT_COLOR = ImVec4(0.75f, 0.57f, 0.06f, 1.0f);
constexpr auto DECOMPOSER_COLOR = ImVec4(0.8f, 0.36f, 0.36f, 1.0f);
constexpr auto SETTER_COLOR = ImVec4(0.6f, 0.36f, 0.8f, 1.0f);
constexpr auto CONTROL_COLOR = ImVec4(0.7f, 0.3f, 0.9f, 1.0f);
constexpr auto PRINT_COLOR = ImVec4(0.2f, 0.7f, 0.9f, 1.0f);
constexpr auto OTHER_COLOR = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

// Pin Type Colors - grouped by category
constexpr auto FLOW_COLOR = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
constexpr auto BOOL_COLOR = ImVec4(0.36f, 0.8f, 0.36f, 1.0f);

// Float types (F1-F4) - shades of purple/magenta
constexpr auto F1_COLOR = ImVec4(0.8f, 0.36f, 0.8f, 1.0f);
constexpr auto F2_COLOR = ImVec4(0.7f, 0.46f, 0.7f, 1.0f);
constexpr auto F3_COLOR = ImVec4(0.6f, 0.56f, 0.6f, 1.0f);
constexpr auto F4_COLOR = ImVec4(0.5f, 0.66f, 0.5f, 1.0f);

// Integer types (I1-I4) - shades of blue
constexpr auto I1_COLOR = ImVec4(0.36f, 0.36f, 0.8f, 1.0f);
constexpr auto I2_COLOR = ImVec4(0.46f, 0.46f, 0.7f, 1.0f);
constexpr auto I3_COLOR = ImVec4(0.56f, 0.56f, 0.6f, 1.0f);
constexpr auto I4_COLOR = ImVec4(0.66f, 0.66f, 0.5f, 1.0f);

// Color types - warm colors
constexpr auto RGB_COLOR = ImVec4(0.8f, 0.4f, 0.2f, 1.0f);
constexpr auto RGBA_COLOR = ImVec4(0.8f, 0.5f, 0.2f, 1.0f);
constexpr auto HSL_COLOR = ImVec4(0.8f, 0.6f, 0.2f, 1.0f);
constexpr auto HSLA_COLOR = ImVec4(0.8f, 0.7f, 0.2f, 1.0f);

// Object types - cyan shades
constexpr auto STRING_COLOR = ImVec4(0.8f, 0.8f, 0.36f, 1.0f);
constexpr auto BLACKHOLE_COLOR = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
constexpr auto STAR_COLOR = ImVec4(1.0f, 0.9f, 0.2f, 1.0f);
constexpr auto OBJECT_COLOR = ImVec4(0.36f, 0.8f, 0.8f, 1.0f);
constexpr auto CAMERA_COLOR = ImVec4(0.2f, 0.6f, 0.8f, 1.0f);
constexpr auto FUNCTION_PIN_COLOR = ImVec4(0.8f, 0.5f, 0.2f, 1.0f);

// UI Constants
constexpr float HEADER_HEIGHT = 24.0f;
constexpr float PIN_SIZE = 12.0f;
constexpr float PIN_MARGIN = 8.0f;
constexpr float SEPARATOR_HEIGHT = 1.0f;
constexpr auto NODE_BG_COLOR = ImVec4(0.13f, 0.14f, 0.15f, 1.0f);
constexpr auto SEPARATOR_COLOR = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

ImVec4 GetNodeColor(AnimationGraph::NodeType type, const std::string &name) {
    switch (type) {
        case AnimationGraph::NodeType::Event: return EVENT_COLOR;
        case AnimationGraph::NodeType::Function: return FUNCTION_COLOR;
        case AnimationGraph::NodeType::Variable: return VARIABLE_COLOR;
        case AnimationGraph::NodeType::Constant: return CONSTANT_COLOR;
        case AnimationGraph::NodeType::Decomposer: return DECOMPOSER_COLOR;
        case AnimationGraph::NodeType::Setter: return SETTER_COLOR;
        case AnimationGraph::NodeType::Control: return CONTROL_COLOR;
        case AnimationGraph::NodeType::Print: return PRINT_COLOR;
        case AnimationGraph::NodeType::Other: return OTHER_COLOR;
        default: return OTHER_COLOR;
    }
}

ImVec4 GetPinColor(AnimationGraph::PinType type) {
    switch (type) {
        case AnimationGraph::PinType::Flow: return FLOW_COLOR;
        case AnimationGraph::PinType::Bool: return BOOL_COLOR;
        case AnimationGraph::PinType::F1: return F1_COLOR;
        case AnimationGraph::PinType::F2: return F2_COLOR;
        case AnimationGraph::PinType::F3: return F3_COLOR;
        case AnimationGraph::PinType::F4: return F4_COLOR;
        case AnimationGraph::PinType::I1: return I1_COLOR;
        case AnimationGraph::PinType::I2: return I2_COLOR;
        case AnimationGraph::PinType::I3: return I3_COLOR;
        case AnimationGraph::PinType::I4: return I4_COLOR;
        case AnimationGraph::PinType::RGB: return RGB_COLOR;
        case AnimationGraph::PinType::RGBA: return RGBA_COLOR;
        case AnimationGraph::PinType::HSL: return HSL_COLOR;
        case AnimationGraph::PinType::HSLA: return HSLA_COLOR;
        case AnimationGraph::PinType::String: return STRING_COLOR;
        case AnimationGraph::PinType::BlackHole: return BLACKHOLE_COLOR;
        case AnimationGraph::PinType::Star: return STAR_COLOR;
        case AnimationGraph::PinType::Object: return OBJECT_COLOR;
        case AnimationGraph::PinType::Camera: return CAMERA_COLOR;
        case AnimationGraph::PinType::Function: return FUNCTION_PIN_COLOR;
        default: return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

void DrawPinIcon(AnimationGraph::PinType type, const ImVec4 &color) {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float radius = 6.0f;
    switch (type) {
        case AnimationGraph::PinType::Flow:
            drawList->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius, ImColor(color));
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            break;
        case AnimationGraph::PinType::Bool:
            drawList->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + radius * 2, pos.y + radius * 2),
                                    ImColor(color), 3.0f);
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            break;
        case AnimationGraph::PinType::I1:
        case AnimationGraph::PinType::I2:
        case AnimationGraph::PinType::I3:
        case AnimationGraph::PinType::I4:
            drawList->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + radius * 2, pos.y + radius * 2),
                                    ImColor(color));
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            break;
        case AnimationGraph::PinType::F1:
        case AnimationGraph::PinType::F2:
        case AnimationGraph::PinType::F3:
        case AnimationGraph::PinType::F4:
            drawList->AddTriangleFilled(ImVec2(pos.x + radius, pos.y), ImVec2(pos.x, pos.y + radius * 2),
                                        ImVec2(pos.x + radius * 2, pos.y + radius * 2), ImColor(color));
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            break;
        case AnimationGraph::PinType::RGB:
        case AnimationGraph::PinType::RGBA:
        case AnimationGraph::PinType::HSL:
        case AnimationGraph::PinType::HSLA:
            // Draw a diamond shape for color types
            drawList->AddQuadFilled(
                ImVec2(pos.x + radius, pos.y),
                ImVec2(pos.x + radius * 2, pos.y + radius),
                ImVec2(pos.x + radius, pos.y + radius * 2),
                ImVec2(pos.x, pos.y + radius),
                ImColor(color));
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            break;
        case AnimationGraph::PinType::BlackHole:
            // Draw a filled circle with a smaller black circle inside
            drawList->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius, ImColor(color));
            drawList->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius * 0.5f, ImColor(0.0f, 0.0f, 0.0f, 1.0f));
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            break;
        case AnimationGraph::PinType::Star:
            // Draw a star-like shape (simplified)
            drawList->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius, ImColor(color));
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            break;
        default:
            drawList->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius, ImColor(color));
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            break;
    }
}

AnimationGraph::AnimationGraph()
    : m_Context(nullptr, ed::DestroyEditor), m_RandomGenerator(std::random_device{}()) {
    m_Context.reset(ed::CreateEditor());
}

AnimationGraph::~AnimationGraph() = default;

int AnimationGraph::GenerateRandomId() {
    std::uniform_int_distribution<int> dist(1000, 999999);
    return dist(m_RandomGenerator);
}

void AnimationGraph::Render() {
    ed::SetCurrentEditor(m_Context.get());
    ed::Begin("Node Editor");

    for (auto &node: m_Nodes) {
        NodeBuilder(node).DrawNode();
    }

    for (const auto &link: m_Links) {
        ed::Link(link.Id, link.StartPinId, link.EndPinId);
    }

    if (ed::BeginCreate()) {
        PinId startPinId, endPinId;
        if (ed::QueryNewLink(&startPinId, &endPinId)) {
            if (startPinId && endPinId && startPinId != endPinId) {
                bool inputPinUsed = false;
                for (const auto& link : m_Links) {
                    if (link.EndPinId == endPinId) {
                        inputPinUsed = true;
                        break;
                    }
                }
                const AnimationGraph::Pin* startPin = nullptr;
                const AnimationGraph::Pin* endPin = nullptr;
                for (const auto& node : m_Nodes) {
                    for (const auto& pin : node.Outputs) {
                        if (pin.Id == startPinId) startPin = &pin;
                    }
                    for (const auto& pin : node.Inputs) {
                        if (pin.Id == endPinId) endPin = &pin;
                    }
                }
                if (!inputPinUsed && startPin && endPin && ArePinsCompatible(startPin->Type, endPin->Type)) {
                    if (ed::AcceptNewItem()) {
                        m_Links.push_back({GenerateRandomId(), startPinId, endPinId});
                    }
                } else {
                    ed::RejectNewItem(ImColor(255, 0, 0, 255), 2.0f);
                }
            }
        }
    }
    ed::EndCreate();

    if (ed::BeginDelete()) {
        ed::LinkId deletedLinkId;
        while (ed::QueryDeletedLink(&deletedLinkId)) {
            auto it = std::remove_if(m_Links.begin(), m_Links.end(),
                                     [&](const Link &l) { return l.Id == deletedLinkId; });
            if (it != m_Links.end()) {
                if (ed::AcceptDeletedItem()) {
                    m_Links.erase(it, m_Links.end());
                }
            }
        }

        ed::NodeId deletedNodeId;
        while (ed::QueryDeletedNode(&deletedNodeId)) {
            auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(),
                                   [&](const Node &n) { return n.Id == deletedNodeId; });
            if (it != m_Nodes.end()) {
                if (ed::AcceptDeletedItem()) {
                    m_Links.erase(
                        std::remove_if(m_Links.begin(), m_Links.end(),
                                       [&](const Link &l) {
                                           for (const auto& pin : it->Inputs) {
                                               if (l.EndPinId == pin.Id) return true;
                                           }
                                           for (const auto& pin : it->Outputs) {
                                               if (l.StartPinId == pin.Id) return true;
                                           }
                                           return false;
                                       }),
                        m_Links.end());
                    m_Nodes.erase(it);
                }
            }
        }
    }
    ed::EndDelete();

    ed::Suspend();
    if (ed::ShowBackgroundContextMenu()) {
        ImGui::OpenPopup("node_create_popup");
    }

    if (ImGui::BeginPopup("node_create_popup")) {
        auto openPopupPosition = ImGui::GetMousePosOnOpeningCurrentPopup();
        ImVec2 mousePos = ed::ScreenToCanvas(openPopupPosition);

        if (ImGui::MenuItem("Add Event Node")) {
            m_Nodes.push_back(CreateEventNode(GenerateRandomId()));
            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
        }
        if (ImGui::MenuItem("Add Print Node")) {
            m_Nodes.push_back(CreatePrintNode(GenerateRandomId()));
            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
        }
        if (ImGui::MenuItem("Add Constant Node")) {
            m_Nodes.push_back(CreateConstantNode(GenerateRandomId(), ""));
            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
        }
        ImGui::EndPopup();
    }
    ed::Resume();

    ed::End();
    ed::SetCurrentEditor(nullptr);
}

bool AnimationGraph::ArePinsCompatible(PinType a, PinType b) {
    return a == b;
}

AnimationGraph::Node AnimationGraph::CreateEventNode(int id) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Event";
    node.Type = NodeType::Event;
    node.SubType = NodeSubType::Start;
    node.Inputs = {};
    node.Outputs = {
        {ed::PinId(id * 10 + 0), "Flow", PinType::Flow, false}
    };
    return node;
}

AnimationGraph::Node AnimationGraph::CreatePrintNode(int id) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Print";
    node.Type = NodeType::Print;
    node.SubType = NodeSubType::None;
    node.Inputs = {
        {ed::PinId(id * 10 + 0), "Flow", PinType::Flow, true},
        {ed::PinId(id * 10 + 1), "Value", PinType::String, true}
    };
    node.Outputs = {
        {ed::PinId(id * 10 + 2), "Flow", PinType::Flow, false}
    };
    return node;
}

AnimationGraph::Node AnimationGraph::CreateConstantNode(int id, const std::string& value) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "String Constant";
    node.Type = NodeType::Constant;
    node.SubType = NodeSubType::None;
    node.Inputs = {};
    node.Outputs = {
        {ed::PinId(id * 10 + 0), "Value", PinType::String, false}
    };
    node.Value = value;
    return node;
}

void AnimationGraph::Serialize(YAML::Emitter& out) const {
    out << YAML::Key << "animation_graph" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "nodes" << YAML::Value << YAML::BeginSeq;
    for (const auto& node : m_Nodes) {
        out << YAML::BeginMap;
        out << YAML::Key << "id" << YAML::Value << node.Id.Get();
        out << YAML::Key << "name" << YAML::Value << node.Name;
        out << YAML::Key << "type" << YAML::Value << static_cast<int>(node.Type);
        out << YAML::Key << "subtype" << YAML::Value << static_cast<int>(node.SubType);
        out << YAML::Key << "inputs" << YAML::Value << YAML::BeginSeq;
        for (const auto& pin : node.Inputs) {
            out << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << pin.Id.Get();
            out << YAML::Key << "name" << YAML::Value << pin.Name;
            out << YAML::Key << "type" << YAML::Value << static_cast<int>(pin.Type);
            out << YAML::Key << "is_input" << YAML::Value << pin.IsInput;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::Key << "outputs" << YAML::Value << YAML::BeginSeq;
        for (const auto& pin : node.Outputs) {
            out << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << pin.Id.Get();
            out << YAML::Key << "name" << YAML::Value << pin.Name;
            out << YAML::Key << "type" << YAML::Value << static_cast<int>(pin.Type);
            out << YAML::Key << "is_input" << YAML::Value << pin.IsInput;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        // Serialize value
        if (std::holds_alternative<std::string>(node.Value)) {
            out << YAML::Key << "value_string" << YAML::Value << std::get<std::string>(node.Value);
        } else if (std::holds_alternative<float>(node.Value)) {
            out << YAML::Key << "value_float" << YAML::Value << std::get<float>(node.Value);
        } else if (std::holds_alternative<int>(node.Value)) {
            out << YAML::Key << "value_int" << YAML::Value << std::get<int>(node.Value);
        }
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::Key << "links" << YAML::Value << YAML::BeginSeq;
    for (const auto& link : m_Links) {
        out << YAML::BeginMap;
        out << YAML::Key << "id" << YAML::Value << link.Id.Get();
        out << YAML::Key << "start_pin_id" << YAML::Value << link.StartPinId.Get();
        out << YAML::Key << "end_pin_id" << YAML::Value << link.EndPinId.Get();
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;
}

void AnimationGraph::Deserialize(const YAML::Node& node) {
    m_Nodes.clear();
    m_Links.clear();
    if (!node["animation_graph"]) return;
    auto graph = node["animation_graph"];
    if (graph["nodes"]) {
        for (const auto& n : graph["nodes"]) {
            Node nodeObj;
            nodeObj.Id = ed::NodeId(n["id"].as<int>());
            nodeObj.Name = n["name"].as<std::string>();
            nodeObj.Type = static_cast<NodeType>(n["type"].as<int>());
            nodeObj.SubType = static_cast<NodeSubType>(n["subtype"].as<int>());
            if (n["value_string"]) nodeObj.Value = n["value_string"].as<std::string>();
            else if (n["value_float"]) nodeObj.Value = n["value_float"].as<float>();
            else if (n["value_int"]) nodeObj.Value = n["value_int"].as<int>();
            if (n["inputs"]) {
                for (const auto& p : n["inputs"]) {
                    Pin pin;
                    pin.Id = ed::PinId(p["id"].as<int>());
                    pin.Name = p["name"].as<std::string>();
                    pin.Type = static_cast<PinType>(p["type"].as<int>());
                    pin.IsInput = p["is_input"].as<bool>();
                    nodeObj.Inputs.push_back(pin);
                }
            }
            if (n["outputs"]) {
                for (const auto& p : n["outputs"]) {
                    Pin pin;
                    pin.Id = ed::PinId(p["id"].as<int>());
                    pin.Name = p["name"].as<std::string>();
                    pin.Type = static_cast<PinType>(p["type"].as<int>());
                    pin.IsInput = p["is_input"].as<bool>();
                    nodeObj.Outputs.push_back(pin);
                }
            }
            m_Nodes.push_back(nodeObj);
        }
    }
    if (graph["links"]) {
        for (const auto& l : graph["links"]) {
            Link linkObj;
            linkObj.Id = ed::LinkId(l["id"].as<int>());
            linkObj.StartPinId = ed::PinId(l["start_pin_id"].as<int>());
            linkObj.EndPinId = ed::PinId(l["end_pin_id"].as<int>());
            m_Links.push_back(linkObj);
        }
    }
}
