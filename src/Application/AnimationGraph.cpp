#define IMGUI_DEFINE_MATH_OPERATORS
#include "AnimationGraph.h"
#include <imgui.h>
#include <imgui_node_editor.h>
#include <algorithm>

namespace ed = ax::NodeEditor;
using ax::NodeEditor::PinId;
using ax::NodeEditor::NodeId;

namespace {
constexpr ImVec4 EVENT_COLOR = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
constexpr ImVec4 FUNCTION_COLOR = ImVec4(0.18f, 0.8f, 0.44f, 1.0f);
constexpr ImVec4 VARIABLE_COLOR = ImVec4(0.95f, 0.77f, 0.06f, 1.0f);
constexpr ImVec4 MATH_COLOR = ImVec4(0.8f, 0.36f, 0.36f, 1.0f);
constexpr ImVec4 BRANCH_COLOR = ImVec4(0.7f, 0.3f, 0.9f, 1.0f);
constexpr ImVec4 PRINT_COLOR = ImVec4(0.2f, 0.7f, 0.9f, 1.0f);
constexpr ImVec4 SEQUENCE_COLOR = ImVec4(0.9f, 0.5f, 0.2f, 1.0f);
constexpr ImVec4 DELAY_COLOR = ImVec4(0.6f, 0.6f, 0.2f, 1.0f);
constexpr ImVec4 FLOW_COLOR = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
constexpr ImVec4 BOOL_COLOR = ImVec4(0.36f, 0.8f, 0.36f, 1.0f);
constexpr ImVec4 INT_COLOR = ImVec4(0.36f, 0.36f, 0.8f, 1.0f);
constexpr ImVec4 FLOAT_COLOR = ImVec4(0.8f, 0.36f, 0.8f, 1.0f);
constexpr ImVec4 STRING_COLOR = ImVec4(0.8f, 0.8f, 0.36f, 1.0f);
constexpr ImVec4 OBJECT_COLOR = ImVec4(0.36f, 0.8f, 0.8f, 1.0f);
constexpr ImVec4 FUNCTION_PIN_COLOR = ImVec4(0.8f, 0.5f, 0.2f, 1.0f);
constexpr ImVec4 DELEGATE_COLOR = ImVec4(0.5f, 0.2f, 0.8f, 1.0f);

ImVec4 GetNodeColor(AnimationGraph::NodeType type, const std::string& name) {
    switch (type) {
        case AnimationGraph::NodeType::Event: return EVENT_COLOR;
        case AnimationGraph::NodeType::Function: return FUNCTION_COLOR;
        case AnimationGraph::NodeType::Variable: return VARIABLE_COLOR;
        case AnimationGraph::NodeType::Math: return MATH_COLOR;
        default:
            if (name == "Branch") return BRANCH_COLOR;
            if (name == "Print") return PRINT_COLOR;
            if (name == "Sequence") return SEQUENCE_COLOR;
            if (name == "Delay") return DELAY_COLOR;
            return ImVec4(1, 1, 1, 1);
    }
}
ImVec4 GetPinColor(AnimationGraph::PinType type) {
    switch (type) {
        case AnimationGraph::PinType::Flow: return FLOW_COLOR;
        case AnimationGraph::PinType::Bool: return BOOL_COLOR;
        case AnimationGraph::PinType::Int: return INT_COLOR;
        case AnimationGraph::PinType::Float: return FLOAT_COLOR;
        case AnimationGraph::PinType::String: return STRING_COLOR;
        case AnimationGraph::PinType::Object: return OBJECT_COLOR;
        case AnimationGraph::PinType::Function: return FUNCTION_PIN_COLOR;
        case AnimationGraph::PinType::Delegate: return DELEGATE_COLOR;
        default: return ImVec4(1, 1, 1, 1);
    }
}
void DrawPinIcon(AnimationGraph::PinType type, const ImVec4& color) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float radius = 6.0f;
    switch (type) {
        case AnimationGraph::PinType::Flow:
            drawList->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius, ImColor(color));
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            break;
        case AnimationGraph::PinType::Bool:
            drawList->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + radius * 2, pos.y + radius * 2), ImColor(color), 3.0f);
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            break;
        case AnimationGraph::PinType::Int:
            drawList->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + radius * 2, pos.y + radius * 2), ImColor(color));
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            break;
        case AnimationGraph::PinType::Float:
            drawList->AddTriangleFilled(ImVec2(pos.x + radius, pos.y), ImVec2(pos.x, pos.y + radius * 2), ImVec2(pos.x + radius * 2, pos.y + radius * 2), ImColor(color));
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            break;
        default:
            drawList->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius, ImColor(color));
            ImGui::Dummy(ImVec2(radius * 2, radius * 2));
            break;
    }
}
}

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
        {m_NextId++, "Duration", PinType::Float, true}
    };
    delayNode.Outputs = {{m_NextId++, "Completed", PinType::Flow, false}};
    m_Nodes.push_back(delayNode);

    Node setVarNode;
    setVarNode.Id = m_NextId++;
    setVarNode.Name = "Set Variable";
    setVarNode.Type = NodeType::Variable;
    setVarNode.Inputs = {
        {m_NextId++, "Value", PinType::Int, true},
        {m_NextId++, "Exec", PinType::Flow, true}
    };
    setVarNode.Outputs = {{m_NextId++, "Exec", PinType::Flow, false}};
    m_Nodes.push_back(setVarNode);

    Node getVarNode;
    getVarNode.Id = m_NextId++;
    getVarNode.Name = "Get Variable";
    getVarNode.Type = NodeType::Variable;
    getVarNode.Inputs = {};
    getVarNode.Outputs = {{m_NextId++, "Value", PinType::Int, false}};
    m_Nodes.push_back(getVarNode);

    Node mathNode;
    mathNode.Id = m_NextId++;
    mathNode.Name = "Add";
    mathNode.Type = NodeType::Math;
    mathNode.Inputs = {
        {m_NextId++, "A", PinType::Float, true},
        {m_NextId++, "B", PinType::Float, true}
    };
    mathNode.Outputs = {{m_NextId++, "Result", PinType::Float, false}};
    m_Nodes.push_back(mathNode);

    // Initial static links (optional, can be empty for more interactivity)
    m_Links.clear();
}

AnimationGraph::~AnimationGraph() = default;

void AnimationGraph::Render() {
    ed::SetCurrentEditor(m_Context.get());
    ed::Begin("Node Editor");

    // Node rendering
    for (const auto &node: m_Nodes) {
        ed::BeginNode(node.Id);
        ImGui::PushStyleColor(ImGuiCol_Header, GetNodeColor(node.Type, node.Name));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, GetNodeColor(node.Type, node.Name));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, GetNodeColor(node.Type, node.Name));
        ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, GetNodeColor(node.Type, node.Name));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
        ImGui::BeginGroup();
        ImGui::TextUnformatted(node.Name.c_str());
        ImGui::Separator();
        ImGui::EndGroup();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        // Inputs
        for (const auto &pin: node.Inputs) {
            ed::BeginPin(pin.Id, ed::PinKind::Input);
            ImGui::PushStyleColor(ImGuiCol_Text, GetPinColor(pin.Type));
            DrawPinIcon(pin.Type, GetPinColor(pin.Type));
            ImGui::SameLine();
            ImGui::Text("%s", pin.Name.c_str());
            ImGui::PopStyleColor();
            ed::EndPin();
        }
        // Outputs
        for (const auto &pin: node.Outputs) {
            ed::BeginPin(pin.Id, ed::PinKind::Output);
            ImGui::PushStyleColor(ImGuiCol_Text, GetPinColor(pin.Type));
            ImGui::Text("%s", pin.Name.c_str());
            ImGui::SameLine();
            DrawPinIcon(pin.Type, GetPinColor(pin.Type));
            ImGui::PopStyleColor();
            ed::EndPin();
        }
        ed::EndNode();
    }

    // Draw links
    for (const auto &link: m_Links) {
        ed::Link(link.Id, link.StartPinId, link.EndPinId);
    }

    // Interactive link creation
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

    // Interactive link deletion
    if (ed::BeginDelete()) {
        ed::LinkId deletedLinkId;
        while (ed::QueryDeletedLink(&deletedLinkId)) {
            auto it = std::remove_if(m_Links.begin(), m_Links.end(), [&](const Link& l) { return l.Id == deletedLinkId; });
            if (it != m_Links.end()) {
                if (ed::AcceptDeletedItem()) {
                    m_Links.erase(it, m_Links.end());
                }
            }
        }
    }
    ed::EndDelete();

    // Context menu for node creation
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
                mathNode.Type = NodeType::Math;
                mathNode.Inputs = {
                    {m_NextId++, "A", PinType::Float, true},
                    {m_NextId++, "B", PinType::Float, true}
                };
                mathNode.Outputs = {{m_NextId++, "Result", PinType::Float, false}};
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
