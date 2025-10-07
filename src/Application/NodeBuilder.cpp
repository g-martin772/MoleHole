#include "NodeBuilder.h"
#include <algorithm>

// External functions from AnimationGraph.cpp
extern ImVec4 GetNodeColor(AnimationGraph::NodeType type, const std::string &name);
extern ImVec4 GetPinColor(AnimationGraph::PinType type);
extern void DrawPinIcon(AnimationGraph::PinType type, const ImVec4 &color);

// UI Constants
constexpr float HEADER_HEIGHT = 28.0f;
constexpr float PIN_SIZE = 12.0f;
constexpr float PIN_MARGIN = 8.0f;
constexpr float NODE_MIN_WIDTH = 150.0f;
constexpr float NODE_PADDING = 8.0f;
constexpr auto NODE_BG_COLOR = ImVec4(0.13f, 0.14f, 0.15f, 1.0f);

NodeBuilder::NodeBuilder(const AnimationGraph::Node &node) : m_Node(node) {
}

void NodeBuilder::DrawNode() {
    ImVec4 headerColor = GetNodeColor(m_Node.Type, m_Node.Name);

    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 0));
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 4.0f);
    ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 0.0f);
    ed::PushStyleVar(ed::StyleVar_HoveredNodeBorderWidth, 0.0f);
    ed::PushStyleVar(ed::StyleVar_SelectedNodeBorderWidth, 0.0f);
    ed::PushStyleVar(ed::StyleVar_HoveredNodeBorderOffset, 0.0f);
    ed::PushStyleVar(ed::StyleVar_SelectedNodeBorderOffset, 0.0f);

    ed::BeginNode(m_Node.Id);

    float nodeWidth = CalculateNodeWidth();

    DrawHeader(headerColor, nodeWidth);
    DrawPinsAndContent(nodeWidth);

    ed::EndNode();
    ed::PopStyleVar(7);
}

// Calculate node width based on content
float NodeBuilder::CalculateNodeWidth() {
    float maxWidth = NODE_MIN_WIDTH;

    // Check header text width
    float headerWidth = ImGui::CalcTextSize(m_Node.Name.c_str()).x + 40.0f; // Extra space for icon
    maxWidth = std::max(maxWidth, headerWidth);

    // Check pin text widths
    for (const auto& pin : m_Node.Inputs) {
        float pinWidth = ImGui::CalcTextSize(pin.Name.c_str()).x + PIN_SIZE + PIN_MARGIN * 2;
        maxWidth = std::max(maxWidth, pinWidth * 1.5f); // Leave space for output pins
    }

    for (const auto& pin : m_Node.Outputs) {
        float pinWidth = ImGui::CalcTextSize(pin.Name.c_str()).x + PIN_SIZE + PIN_MARGIN * 2;
        maxWidth = std::max(maxWidth, pinWidth * 1.5f); // Leave space for input pins
    }

    return maxWidth;
}

void NodeBuilder::DrawHeader(const ImVec4 &headerColor, float nodeWidth) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 headerStart = ImGui::GetCursorScreenPos();
    ImVec2 headerEnd = ImVec2(headerStart.x + nodeWidth, headerStart.y + HEADER_HEIGHT);

    // Draw header background
    drawList->AddRectFilled(headerStart, headerEnd, ImColor(headerColor), 4.0f, ImDrawFlags_RoundCornersTop);

    // Position cursor for icon and text
    ImGui::SetCursorScreenPos(ImVec2(headerStart.x + NODE_PADDING, headerStart.y + (HEADER_HEIGHT - 16.0f) * 0.5f));

    DrawNodeIcon();
    ImGui::SameLine(0, 4.0f); // Small gap between icon and text

    // Center text vertically
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (16.0f - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", m_Node.Name.c_str());

    // Move cursor to end of header
    ImGui::SetCursorScreenPos(ImVec2(headerStart.x, headerEnd.y));
    ImGui::Dummy(ImVec2(nodeWidth, 0));
}

void NodeBuilder::DrawNodeIcon() const {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 iconPos = ImGui::GetCursorScreenPos();
    float iconSize = 16.0f;
    auto iconColor = ImVec4(1.0f, 1.0f, 1.0f, 0.9f);

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

void NodeBuilder::DrawPinsAndContent(float nodeWidth) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 contentStart = ImGui::GetCursorScreenPos();

    size_t maxPins = std::max(m_Node.Inputs.size(), m_Node.Outputs.size());
    float contentHeight = maxPins > 0 ? maxPins * (PIN_SIZE + 4.0f) + NODE_PADDING : NODE_PADDING;

    // Draw content background
    ImVec2 contentEnd = ImVec2(contentStart.x + nodeWidth, contentStart.y + contentHeight);
    drawList->AddRectFilled(contentStart, contentEnd, ImColor(NODE_BG_COLOR), 4.0f, ImDrawFlags_RoundCornersBottom);

    // Draw pins
    for (size_t i = 0; i < maxPins; ++i) {
        ImVec2 rowStart = ImVec2(contentStart.x, contentStart.y + NODE_PADDING/2 + i * (PIN_SIZE + 4.0f));

        // Draw input pin
        if (i < m_Node.Inputs.size()) {
            ImGui::SetCursorScreenPos(ImVec2(rowStart.x + NODE_PADDING, rowStart.y));
            DrawInputPin(m_Node.Inputs[i]);
        }

        // Draw output pin
        if (i < m_Node.Outputs.size()) {
            const auto& pin = m_Node.Outputs[i];
            float textWidth = ImGui::CalcTextSize(pin.Name.c_str()).x;
            ImVec2 outputPos = ImVec2(contentEnd.x - NODE_PADDING - textWidth - PIN_SIZE - 4.0f, rowStart.y);
            ImGui::SetCursorScreenPos(outputPos);
            DrawOutputPin(pin);
        }
    }

    // Move cursor to bottom-left of content and create a zero-height spacer with the desired width
    // to finalize layout height without inflating node width.
    ImGui::SetCursorScreenPos(ImVec2(contentStart.x, contentEnd.y));
    ImGui::Dummy(ImVec2(nodeWidth, 0));
}

void NodeBuilder::DrawInputPin(const AnimationGraph::Pin &pin) {
    ed::BeginPin(pin.Id, ed::PinKind::Input);
    DrawPinIcon(pin.Type, GetPinColor(pin.Type));
    ImGui::SameLine(0, 4.0f);
    ImGui::Text("%s", pin.Name.c_str());
    ed::EndPin();
}

void NodeBuilder::DrawOutputPin(const AnimationGraph::Pin &pin) {
    ed::BeginPin(pin.Id, ed::PinKind::Output);
    ImGui::Text("%s", pin.Name.c_str());
    ImGui::SameLine(0, 4.0f);
    DrawPinIcon(pin.Type, GetPinColor(pin.Type));
    ed::EndPin();
}
