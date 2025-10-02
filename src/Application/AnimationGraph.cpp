#define IMGUI_DEFINE_MATH_OPERATORS
#include "AnimationGraph.h"
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

class NodeBuilder {
public:
    explicit NodeBuilder(const AnimationGraph::Node &node) : m_Node(node) {
    }

    void DrawNode() {
        ImVec4 headerColor = GetNodeColor(m_Node.Type, m_Node.Name);
        headerColor.w = 0.9f;

        ed::BeginNode(m_Node.Id);

        DrawHeader(headerColor);
        DrawSeparator();
        DrawPinsAndContent();

        ed::EndNode();
    }

private:
    const AnimationGraph::Node &m_Node;

    void DrawHeader(const ImVec4 &headerColor) {
        ImGui::BeginGroup();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, headerColor);
        std::string headerID = "header_" + std::to_string(m_Node.Id.Get());
        ImGui::BeginChild(headerID.c_str(), ImVec2(0, HEADER_HEIGHT), false, ImGuiWindowFlags_NoScrollbar);

        ImGui::SetCursorPosY((HEADER_HEIGHT - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::SetCursorPosX(8.0f);

        DrawNodeIcon();
        ImGui::SameLine();
        ImGui::Text("%s", m_Node.Name.c_str());

        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::EndGroup();
    }

    void DrawNodeIcon() const {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        ImVec2 iconPos = ImGui::GetCursorScreenPos();
        float iconSize = 16.0f;
        auto iconColor = ImVec4(1.0f, 1.0f, 1.0f, 0.8f);

        switch (m_Node.Type) {
            case AnimationGraph::NodeType::Event:
                drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                                        ImColor(iconColor), 2.0f);
                break;
            case AnimationGraph::NodeType::Function:
                drawList->AddCircleFilled(ImVec2(iconPos.x + iconSize / 2, iconPos.y + iconSize / 2),
                                          iconSize / 2, ImColor(iconColor));
                break;
            case AnimationGraph::NodeType::Variable:
                drawList->AddTriangleFilled(ImVec2(iconPos.x + iconSize / 2, iconPos.y),
                                            ImVec2(iconPos.x, iconPos.y + iconSize),
                                            ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                                            ImColor(iconColor));
                break;
            case AnimationGraph::NodeType::Constant:
                drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                                        ImColor(iconColor));
                break;
            case AnimationGraph::NodeType::Decomposer:
                drawList->AddCircleFilled(ImVec2(iconPos.x + iconSize / 2, iconPos.y + iconSize / 2),
                                          iconSize / 3, ImColor(iconColor));
                break;
            case AnimationGraph::NodeType::Setter:
                drawList->AddTriangleFilled(ImVec2(iconPos.x, iconPos.y + iconSize / 2),
                                            ImVec2(iconPos.x + iconSize, iconPos.y),
                                            ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                                            ImColor(iconColor));
                break;
            case AnimationGraph::NodeType::Control:
                drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                                        ImColor(iconColor), iconSize / 4);
                break;
            case AnimationGraph::NodeType::Print:
                drawList->AddRect(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                                  ImColor(iconColor), 2.0f, 0, 2.0f);
                break;
            default:
                drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                                        ImColor(iconColor));
                break;
        }

        ImGui::Dummy(ImVec2(iconSize, iconSize));
    }

    static void DrawSeparator() {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        ImVec2 separatorStart = ImGui::GetCursorScreenPos();
        auto separatorEnd = ImVec2(separatorStart.x + ImGui::GetContentRegionAvail().x,
                                     separatorStart.y + SEPARATOR_HEIGHT);

        drawList->AddRectFilled(separatorStart, separatorEnd, ImColor(SEPARATOR_COLOR));
        ImGui::Dummy(ImVec2(0, SEPARATOR_HEIGHT));
    }

    void DrawPinsAndContent() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, NODE_BG_COLOR);

        size_t maxPins = std::max(m_Node.Inputs.size(), m_Node.Outputs.size());

        for (size_t i = 0; i < maxPins; ++i) {
            ImGui::BeginGroup();

            if (i < m_Node.Inputs.size()) {
                DrawInputPin(m_Node.Inputs[i]);
            } else {
                ImGui::Dummy(ImVec2(PIN_SIZE + PIN_MARGIN, PIN_SIZE));
            }

            ImGui::SameLine();

            float availableWidth = ImGui::GetContentRegionAvail().x - (PIN_SIZE + PIN_MARGIN);
            ImGui::Dummy(ImVec2(availableWidth, PIN_SIZE));

            ImGui::SameLine();

            if (i < m_Node.Outputs.size()) {
                DrawOutputPin(m_Node.Outputs[i]);
            } else {
                ImGui::Dummy(ImVec2(PIN_SIZE + PIN_MARGIN, PIN_SIZE));
            }

            ImGui::EndGroup();

            if (i < maxPins - 1) {
                ImGui::Dummy(ImVec2(0, 4.0f));
            }
        }

        ImGui::PopStyleColor();
    }

    static void DrawInputPin(const AnimationGraph::Pin &pin) {
        ed::BeginPin(pin.Id, ed::PinKind::Input);
        DrawPinIcon(pin.Type, GetPinColor(pin.Type));
        ImGui::SameLine();
        ImGui::Text("%s", pin.Name.c_str());
        ed::EndPin();
    }

    static void DrawOutputPin(const AnimationGraph::Pin &pin) {
        float textWidth = ImGui::CalcTextSize(pin.Name.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - textWidth - PIN_SIZE - PIN_MARGIN);

        ed::BeginPin(pin.Id, ed::PinKind::Output);
        ImGui::Text("%s", pin.Name.c_str());
        ImGui::SameLine();
        DrawPinIcon(pin.Type, GetPinColor(pin.Type));
        ed::EndPin();
    }
};


AnimationGraph::AnimationGraph()
    : m_Context(nullptr, ed::DestroyEditor), m_NextId(1) {
    m_Context.reset(ed::CreateEditor());
    m_Nodes.clear();
    m_Links.clear();

    Node eventNode;
    eventNode.Id = m_NextId++;
    eventNode.Name = "On Begin Play";
    eventNode.Type = NodeType::Event;
    eventNode.Inputs = {{m_NextId++, "Exec", PinType::Flow, true}};
    eventNode.Outputs = {{m_NextId++, "Exec", PinType::Flow, false}};
    m_Nodes.push_back(eventNode);

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

    Node sequenceNode;
    sequenceNode.Id = m_NextId++;
    sequenceNode.Name = "Sequence";
    sequenceNode.Type = NodeType::Function;
    sequenceNode.Inputs = {{m_NextId++, "Exec", PinType::Flow, true}};
    sequenceNode.Outputs = {
        {m_NextId++, "Then 0", PinType::Flow, false},
        {m_NextId++, "Then 1", PinType::Flow, false}
    };
    m_Nodes.push_back(sequenceNode);

    Node delayNode;
    delayNode.Id = m_NextId++;
    delayNode.Name = "Delay";
    delayNode.Type = NodeType::Function;
    delayNode.Inputs = {
        {m_NextId++, "Exec", PinType::Flow, true},
        {m_NextId++, "Duration", PinType::F1, true}
    };
    delayNode.Outputs = {{m_NextId++, "Completed", PinType::Flow, false}};
    m_Nodes.push_back(delayNode);

    Node setVarNode;
    setVarNode.Id = m_NextId++;
    setVarNode.Name = "Set Variable";
    setVarNode.Type = NodeType::Variable;
    setVarNode.Inputs = {
        {m_NextId++, "Value", PinType::I1, true},
        {m_NextId++, "Exec", PinType::Flow, true}
    };
    setVarNode.Outputs = {{m_NextId++, "Exec", PinType::Flow, false}};
    m_Nodes.push_back(setVarNode);

    Node getVarNode;
    getVarNode.Id = m_NextId++;
    getVarNode.Name = "Get Variable";
    getVarNode.Type = NodeType::Variable;
    getVarNode.Inputs = {};
    getVarNode.Outputs = {{m_NextId++, "Value", PinType::I1, false}};
    m_Nodes.push_back(getVarNode);

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

    m_Links.clear();
}

AnimationGraph::~AnimationGraph() = default;

void AnimationGraph::Render() {
    ed::SetCurrentEditor(m_Context.get());
    ed::Begin("Node Editor");

    for (const auto &node: m_Nodes) {
        NodeBuilder(node).DrawNode();
    }

    for (const auto &link: m_Links) {
        ed::Link(link.Id, link.StartPinId, link.EndPinId);
    }

    if (ed::BeginCreate()) {
        PinId startPinId, endPinId;
        if (ed::QueryNewLink(&startPinId, &endPinId)) {
            if (startPinId && endPinId && startPinId != endPinId) {
                if (ed::AcceptNewItem()) {
                    m_Links.push_back({m_NextId++, startPinId, endPinId});
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
