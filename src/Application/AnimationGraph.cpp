#define IMGUI_DEFINE_MATH_OPERATORS
#include "AnimationGraph.h"
#include "NodeBuilder.h"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <algorithm>

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
    : m_Context(nullptr, ed::DestroyEditor), m_NextId(1) {
    m_Context.reset(ed::CreateEditor());
    m_Nodes.clear();
    m_Links.clear();

    m_Nodes.push_back(CreateEventNode(m_NextId++));
    m_Nodes.push_back(CreatePrintNode(m_NextId++));
    m_Nodes.push_back(CreateConstantNode(m_NextId++, "Hello World"));
    m_Nodes.push_back(CreateConstantNode(m_NextId++, "Sample String"));

    m_Links.clear();
}

AnimationGraph::~AnimationGraph() = default;

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
                        m_Links.push_back({m_NextId++, startPinId, endPinId});
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
    }
    ed::EndDelete();

    if (ed::ShowBackgroundContextMenu()) {
        if (ImGui::BeginPopup("node_create_popup")) {
            if (ImGui::MenuItem("Add Print Node")) {
                Node printNode;
                printNode.Id = m_NextId++;
                printNode.Name = "Print";
                printNode.Type = NodeType::Function;
                printNode.Inputs = {
                    {m_NextId++, "Exec", PinType::Flow, true},
                    {m_NextId++, "Value", PinType::String, true}
                };
                printNode.Outputs = {{m_NextId++, "Exec", PinType::Flow, false}};
                m_Nodes.push_back(printNode);
                ed::SetNodePosition(printNode.Id, ed::ScreenToCanvas(ImGui::GetMousePos()));
            }
            if (ImGui::MenuItem("Add Math Node")) {
                Node mathNode;
                mathNode.Id = m_NextId++;
                mathNode.Name = "Add";
                mathNode.Type = NodeType::Function;
                mathNode.Inputs = {
                    {m_NextId++, "A", PinType::F1, true},
                    {m_NextId++, "B", PinType::F1, true}
                };
                mathNode.Outputs = {{m_NextId++, "Result", PinType::F1, false}};
                m_Nodes.push_back(mathNode);
                ed::SetNodePosition(mathNode.Id, ed::ScreenToCanvas(ImGui::GetMousePos()));
            }
            if (ImGui::MenuItem("Add Branch Node")) {
                Node branchNode;
                branchNode.Id = m_NextId++;
                branchNode.Name = "Branch";
                branchNode.Type = NodeType::Function;
                branchNode.Inputs = {
                    {m_NextId++, "Exec", PinType::Flow, true},
                    {m_NextId++, "Condition", PinType::Bool, true}
                };
                branchNode.Outputs = {
                    {m_NextId++, "True", PinType::Flow, false},
                    {m_NextId++, "False", PinType::Flow, false}
                };
                m_Nodes.push_back(branchNode);
                ed::SetNodePosition(branchNode.Id, ed::ScreenToCanvas(ImGui::GetMousePos()));
            }
            ImGui::EndPopup();
        } else {
            ImGui::OpenPopup("node_create_popup");
        }
    }

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
