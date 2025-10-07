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
        NodeBuilder(node, m_Variables, m_SceneObjects).DrawNode();
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

        static char searchBuffer[256] = "";
        ImGui::SetNextItemWidth(250);
        ImGui::InputTextWithHint("##search", "Search nodes...", searchBuffer, sizeof(searchBuffer));
        ImGui::Separator();

        std::string search = searchBuffer;
        std::transform(search.begin(), search.end(), search.begin(), ::tolower);

        auto matchesSearch = [&](const std::string& name) {
            if (search.empty()) return true;
            std::string lowerName = name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
            return lowerName.find(search) != std::string::npos;
        };

        // If searching, show flat list
        if (!search.empty()) {
            // Events
            if (matchesSearch("Scene Start") && ImGui::MenuItem("Scene Start")) {
                m_Nodes.push_back(CreateEventNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Tick Update") && ImGui::MenuItem("Tick Update")) {
                m_Nodes.push_back(CreateTickEventNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }

            // Scene Access
            if (matchesSearch("Get BlackHole From Scene") && ImGui::MenuItem("Get BlackHole From Scene")) {
                m_Nodes.push_back(CreateGetBlackHoleFromSceneNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Get Camera From Scene") && ImGui::MenuItem("Get Camera From Scene")) {
                m_Nodes.push_back(CreateGetCameraFromSceneNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }

            // Constants
            if (matchesSearch("Float") && ImGui::MenuItem("Float")) {
                m_Nodes.push_back(CreateConstantFloatNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Vec2") && ImGui::MenuItem("Vec2")) {
                m_Nodes.push_back(CreateConstantVec2Node(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Vec3") && ImGui::MenuItem("Vec3")) {
                m_Nodes.push_back(CreateConstantVec3Node(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Vec4") && ImGui::MenuItem("Vec4")) {
                m_Nodes.push_back(CreateConstantVec4Node(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Int") && ImGui::MenuItem("Int")) {
                m_Nodes.push_back(CreateConstantIntNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Bool") && ImGui::MenuItem("Bool")) {
                m_Nodes.push_back(CreateConstantBoolNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("String") && ImGui::MenuItem("String")) {
                m_Nodes.push_back(CreateConstantStringNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }

            // Math nodes
            const std::vector<std::pair<const char*, PinType>> types = {
                {"Float", PinType::F1}, {"Vec2", PinType::F2}, {"Vec3", PinType::F3}, {"Vec4", PinType::F4}
            };

            for (const auto& [name, type] : types) {
                std::string addName = std::string("Add ") + name;
                std::string subName = std::string("Subtract ") + name;
                std::string mulName = std::string("Multiply ") + name;
                std::string divName = std::string("Divide ") + name;
                std::string minName = std::string("Min ") + name;
                std::string maxName = std::string("Max ") + name;
                std::string sqrtName = std::string("Sqrt ") + name;
                std::string negateName = std::string("Negate ") + name;
                std::string lerpName = std::string("Lerp ") + name;
                std::string clampName = std::string("Clamp ") + name;

                if (matchesSearch(addName) && ImGui::MenuItem(addName.c_str())) {
                    m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Add, type));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (matchesSearch(subName) && ImGui::MenuItem(subName.c_str())) {
                    m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Sub, type));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (matchesSearch(mulName) && ImGui::MenuItem(mulName.c_str())) {
                    m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Mul, type));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (matchesSearch(divName) && ImGui::MenuItem(divName.c_str())) {
                    m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Div, type));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (matchesSearch(minName) && ImGui::MenuItem(minName.c_str())) {
                    m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Min, type));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (matchesSearch(maxName) && ImGui::MenuItem(maxName.c_str())) {
                    m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Max, type));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (matchesSearch(sqrtName) && ImGui::MenuItem(sqrtName.c_str())) {
                    m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Sqrt, type));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (matchesSearch(negateName) && ImGui::MenuItem(negateName.c_str())) {
                    m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Negate, type));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (matchesSearch(lerpName) && ImGui::MenuItem(lerpName.c_str())) {
                    m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Lerp, type));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (matchesSearch(clampName) && ImGui::MenuItem(clampName.c_str())) {
                    m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Clamp, type));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }

                if (type != PinType::F1) {
                    std::string lengthName = std::string("Length ") + name;
                    std::string distName = std::string("Distance ") + name;
                    if (matchesSearch(lengthName) && ImGui::MenuItem(lengthName.c_str())) {
                        m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Length, type));
                        ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                    }
                    if (matchesSearch(distName) && ImGui::MenuItem(distName.c_str())) {
                        m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Distance, type));
                        ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                    }
                }
            }

            // Trig functions
            if (matchesSearch("Sin") && ImGui::MenuItem("Sin")) {
                m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Sin, PinType::F1));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Cos") && ImGui::MenuItem("Cos")) {
                m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Cos, PinType::F1));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Tan") && ImGui::MenuItem("Tan")) {
                m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Tan, PinType::F1));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }

            // Logic
            if (matchesSearch("And") && ImGui::MenuItem("And")) {
                m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::And, PinType::Bool));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Or") && ImGui::MenuItem("Or")) {
                m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Or, PinType::Bool));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }

            // Control flow
            if (matchesSearch("If") && ImGui::MenuItem("If")) {
                m_Nodes.push_back(CreateIfNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Branch") && ImGui::MenuItem("Branch")) {
                m_Nodes.push_back(CreateBranchNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("For Loop") && ImGui::MenuItem("For Loop")) {
                m_Nodes.push_back(CreateForNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }

            // Object accessors
            if (matchesSearch("Get BlackHole") && ImGui::MenuItem("Get BlackHole Properties")) {
                m_Nodes.push_back(CreateBlackHoleDecomposerNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Set BlackHole") && ImGui::MenuItem("Set BlackHole Properties")) {
                m_Nodes.push_back(CreateBlackHoleSetterNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Get Camera") && ImGui::MenuItem("Get Camera Properties")) {
                m_Nodes.push_back(CreateCameraDecomposerNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
            if (matchesSearch("Set Camera") && ImGui::MenuItem("Set Camera Properties")) {
                m_Nodes.push_back(CreateCameraSetterNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }

            // Variables
            const std::vector<std::pair<const char*, PinType>> varTypes = {
                {"Float", PinType::F1}, {"Vec2", PinType::F2}, {"Vec3", PinType::F3}, {"Vec4", PinType::F4},
                {"Int", PinType::I1}, {"Bool", PinType::Bool}, {"String", PinType::String},
                {"BlackHole", PinType::BlackHole}, {"Camera", PinType::Camera}
            };

            for (const auto& [name, type] : varTypes) {
                std::string getName = std::string("Get ") + name + " Variable";
                std::string setName = std::string("Set ") + name + " Variable";
                if (matchesSearch(getName) && ImGui::MenuItem(getName.c_str())) {
                    m_Nodes.push_back(CreateVariableGetNode(GenerateRandomId(), type));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (matchesSearch(setName) && ImGui::MenuItem(setName.c_str())) {
                    m_Nodes.push_back(CreateVariableSetNode(GenerateRandomId(), type));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
            }

            // Utility
            if (matchesSearch("Print") && ImGui::MenuItem("Print")) {
                m_Nodes.push_back(CreatePrintNode(GenerateRandomId()));
                ed::SetNodePosition(m_Nodes.back().Id, mousePos);
            }
        } else {
            // No search - show categorized menu

            // Scene
            if (ImGui::BeginMenu("Scene")) {
                if (ImGui::MenuItem("Get BlackHole From Scene")) {
                    m_Nodes.push_back(CreateGetBlackHoleFromSceneNode(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (ImGui::MenuItem("Get Camera From Scene")) {
                    m_Nodes.push_back(CreateGetCameraFromSceneNode(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                ImGui::EndMenu();
            }

            // Events
            if (ImGui::BeginMenu("Events")) {
                if (ImGui::MenuItem("Start Event")) {
                    m_Nodes.push_back(CreateEventNode(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (ImGui::MenuItem("Tick Update")) {
                    m_Nodes.push_back(CreateTickEventNode(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                ImGui::EndMenu();
            }

            // Constants
            if (ImGui::BeginMenu("Constants")) {
                if (ImGui::MenuItem("Float")) {
                    m_Nodes.push_back(CreateConstantFloatNode(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (ImGui::MenuItem("Vec2")) {
                    m_Nodes.push_back(CreateConstantVec2Node(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (ImGui::MenuItem("Vec3")) {
                    m_Nodes.push_back(CreateConstantVec3Node(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (ImGui::MenuItem("Vec4")) {
                    m_Nodes.push_back(CreateConstantVec4Node(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (ImGui::MenuItem("Int")) {
                    m_Nodes.push_back(CreateConstantIntNode(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (ImGui::MenuItem("Bool")) {
                    m_Nodes.push_back(CreateConstantBoolNode(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (ImGui::MenuItem("String")) {
                    m_Nodes.push_back(CreateConstantStringNode(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                ImGui::EndMenu();
            }

            // Math Functions
            if (ImGui::BeginMenu("Math")) {
                const std::vector<std::pair<const char*, PinType>> types = {
                    {"Float", PinType::F1}, {"Vec2", PinType::F2}, {"Vec3", PinType::F3}, {"Vec4", PinType::F4}
                };

                if (ImGui::BeginMenu("Add")) {
                    for (const auto& [name, type] : types) {
                        std::string itemName = std::string("Add ") + name;
                        if (ImGui::MenuItem(itemName.c_str())) {
                            m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Add, type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Subtract")) {
                    for (const auto& [name, type] : types) {
                        std::string itemName = std::string("Subtract ") + name;
                        if (ImGui::MenuItem(itemName.c_str())) {
                            m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Sub, type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Multiply")) {
                    for (const auto& [name, type] : types) {
                        std::string itemName = std::string("Multiply ") + name;
                        if (ImGui::MenuItem(itemName.c_str())) {
                            m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Mul, type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Divide")) {
                    for (const auto& [name, type] : types) {
                        std::string itemName = std::string("Divide ") + name;
                        if (ImGui::MenuItem(itemName.c_str())) {
                            m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Div, type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Min/Max")) {
                    for (const auto& [name, type] : types) {
                        std::string minName = std::string("Min ") + name;
                        std::string maxName = std::string("Max ") + name;
                        if (ImGui::MenuItem(minName.c_str())) {
                            m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Min, type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                        if (ImGui::MenuItem(maxName.c_str())) {
                            m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Max, type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Trigonometry")) {
                    if (ImGui::MenuItem("Sin")) {
                        m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Sin, PinType::F1));
                        ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                    }
                    if (ImGui::MenuItem("Cos")) {
                        m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Cos, PinType::F1));
                        ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                    }
                    if (ImGui::MenuItem("Tan")) {
                        m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Tan, PinType::F1));
                        ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Vector")) {
                    for (const auto& [name, type] : types) {
                        if (type == PinType::F1) continue;
                        std::string lengthName = std::string("Length ") + name;
                        std::string distName = std::string("Distance ") + name;
                        if (ImGui::MenuItem(lengthName.c_str())) {
                            m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Length, type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                        if (ImGui::MenuItem(distName.c_str())) {
                            m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Distance, type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Interpolation")) {
                    for (const auto& [name, type] : types) {
                        std::string lerpName = std::string("Lerp ") + name;
                        std::string clampName = std::string("Clamp ") + name;
                        if (ImGui::MenuItem(lerpName.c_str())) {
                            m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Lerp, type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                        if (ImGui::MenuItem(clampName.c_str())) {
                            m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Clamp, type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Other")) {
                    for (const auto& [name, type] : types) {
                        std::string sqrtName = std::string("Sqrt ") + name;
                        std::string negateName = std::string("Negate ") + name;
                        if (ImGui::MenuItem(sqrtName.c_str())) {
                            m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Sqrt, type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                        if (ImGui::MenuItem(negateName.c_str())) {
                            m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Negate, type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                    }
                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }

            // Logic
            if (ImGui::BeginMenu("Logic")) {
                if (ImGui::MenuItem("And")) {
                    m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::And, PinType::Bool));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (ImGui::MenuItem("Or")) {
                    m_Nodes.push_back(CreateMathNode(GenerateRandomId(), NodeSubType::Or, PinType::Bool));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                ImGui::EndMenu();
            }

            // Control Flow
            if (ImGui::BeginMenu("Control Flow")) {
                if (ImGui::MenuItem("If")) {
                    m_Nodes.push_back(CreateIfNode(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (ImGui::MenuItem("Branch")) {
                    m_Nodes.push_back(CreateBranchNode(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                if (ImGui::MenuItem("For Loop")) {
                    m_Nodes.push_back(CreateForNode(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                ImGui::EndMenu();
            }

            // Objects
            if (ImGui::BeginMenu("Objects")) {
                if (ImGui::BeginMenu("BlackHole")) {
                    if (ImGui::MenuItem("Get BlackHole")) {
                        m_Nodes.push_back(CreateBlackHoleDecomposerNode(GenerateRandomId()));
                        ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                    }
                    if (ImGui::MenuItem("Set BlackHole")) {
                        m_Nodes.push_back(CreateBlackHoleSetterNode(GenerateRandomId()));
                        ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Camera")) {
                    if (ImGui::MenuItem("Get Camera")) {
                        m_Nodes.push_back(CreateCameraDecomposerNode(GenerateRandomId()));
                        ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                    }
                    if (ImGui::MenuItem("Set Camera")) {
                        m_Nodes.push_back(CreateCameraSetterNode(GenerateRandomId()));
                        ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            // Utility
            if (ImGui::BeginMenu("Utility")) {
                if (ImGui::MenuItem("Print")) {
                    m_Nodes.push_back(CreatePrintNode(GenerateRandomId()));
                    ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                }
                ImGui::EndMenu();
            }

            // Variables
            if (ImGui::BeginMenu("Variables")) {
                const std::vector<std::pair<const char*, PinType>> varTypes = {
                    {"Float", PinType::F1}, {"Vec2", PinType::F2}, {"Vec3", PinType::F3}, {"Vec4", PinType::F4},
                    {"Int", PinType::I1}, {"Bool", PinType::Bool}, {"String", PinType::String},
                    {"BlackHole", PinType::BlackHole}, {"Camera", PinType::Camera}
                };

                if (ImGui::BeginMenu("Get Variable")) {
                    for (const auto& [name, type] : varTypes) {
                        std::string itemName = std::string("Get ") + name + " Variable";
                        if (ImGui::MenuItem(itemName.c_str())) {
                            m_Nodes.push_back(CreateVariableGetNode(GenerateRandomId(), type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Set Variable")) {
                    for (const auto& [name, type] : varTypes) {
                        std::string itemName = std::string("Set ") + name + " Variable";
                        if (ImGui::MenuItem(itemName.c_str())) {
                            m_Nodes.push_back(CreateVariableSetNode(GenerateRandomId(), type));
                            ed::SetNodePosition(m_Nodes.back().Id, mousePos);
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
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
    node.Name = "Scene Start";
    node.Type = NodeType::Event;
    node.SubType = NodeSubType::Start;
    node.Inputs = {};
    node.Outputs = {
        {ed::PinId(id * 10 + 0), "Flow", PinType::Flow, false}
    };
    return node;
}

AnimationGraph::Node AnimationGraph::CreateTickEventNode(int id) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Tick Update";
    node.Type = NodeType::Event;
    node.SubType = NodeSubType::Tick;
    node.Inputs = {};
    node.Outputs = {
        {ed::PinId(id * 10 + 0), "Flow", PinType::Flow, false},
        {ed::PinId(id * 10 + 1), "Delta Time", PinType::F1, false}
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

AnimationGraph::Node AnimationGraph::CreateMathNode(int id, NodeSubType subType, PinType valueType) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Type = NodeType::Function;
    node.SubType = subType;

    std::string typeName;
    switch (valueType) {
        case PinType::F1: typeName = "Float"; break;
        case PinType::F2: typeName = "Vec2"; break;
        case PinType::F3: typeName = "Vec3"; break;
        case PinType::F4: typeName = "Vec4"; break;
        case PinType::I1: typeName = "Int"; break;
        case PinType::Bool: typeName = "Bool"; break;
        default: typeName = ""; break;
    }

    switch (subType) {
        case NodeSubType::Add:
            node.Name = "Add " + typeName;
            node.Inputs = {
                {ed::PinId(id * 10 + 0), "A", valueType, true},
                {ed::PinId(id * 10 + 1), "B", valueType, true}
            };
            node.Outputs = {{ed::PinId(id * 10 + 2), "Result", valueType, false}};
            break;
        case NodeSubType::Sub:
            node.Name = "Subtract " + typeName;
            node.Inputs = {
                {ed::PinId(id * 10 + 0), "A", valueType, true},
                {ed::PinId(id * 10 + 1), "B", valueType, true}
            };
            node.Outputs = {{ed::PinId(id * 10 + 2), "Result", valueType, false}};
            break;
        case NodeSubType::Mul:
            node.Name = "Multiply " + typeName;
            node.Inputs = {
                {ed::PinId(id * 10 + 0), "A", valueType, true},
                {ed::PinId(id * 10 + 1), "B", valueType, true}
            };
            node.Outputs = {{ed::PinId(id * 10 + 2), "Result", valueType, false}};
            break;
        case NodeSubType::Div:
            node.Name = "Divide " + typeName;
            node.Inputs = {
                {ed::PinId(id * 10 + 0), "A", valueType, true},
                {ed::PinId(id * 10 + 1), "B", valueType, true}
            };
            node.Outputs = {{ed::PinId(id * 10 + 2), "Result", valueType, false}};
            break;
        case NodeSubType::Min:
            node.Name = "Min " + typeName;
            node.Inputs = {
                {ed::PinId(id * 10 + 0), "A", valueType, true},
                {ed::PinId(id * 10 + 1), "B", valueType, true}
            };
            node.Outputs = {{ed::PinId(id * 10 + 2), "Result", valueType, false}};
            break;
        case NodeSubType::Max:
            node.Name = "Max " + typeName;
            node.Inputs = {
                {ed::PinId(id * 10 + 0), "A", valueType, true},
                {ed::PinId(id * 10 + 1), "B", valueType, true}
            };
            node.Outputs = {{ed::PinId(id * 10 + 2), "Result", valueType, false}};
            break;
        case NodeSubType::Negate:
            node.Name = "Negate " + typeName;
            node.Inputs = {{ed::PinId(id * 10 + 0), "Value", valueType, true}};
            node.Outputs = {{ed::PinId(id * 10 + 1), "Result", valueType, false}};
            break;
        case NodeSubType::Sin:
            node.Name = "Sin";
            node.Inputs = {{ed::PinId(id * 10 + 0), "Value", PinType::F1, true}};
            node.Outputs = {{ed::PinId(id * 10 + 1), "Result", PinType::F1, false}};
            break;
        case NodeSubType::Cos:
            node.Name = "Cos";
            node.Inputs = {{ed::PinId(id * 10 + 0), "Value", PinType::F1, true}};
            node.Outputs = {{ed::PinId(id * 10 + 1), "Result", PinType::F1, false}};
            break;
        case NodeSubType::Tan:
            node.Name = "Tan";
            node.Inputs = {{ed::PinId(id * 10 + 0), "Value", PinType::F1, true}};
            node.Outputs = {{ed::PinId(id * 10 + 1), "Result", PinType::F1, false}};
            break;
        case NodeSubType::Sqrt:
            node.Name = "Sqrt " + typeName;
            node.Inputs = {{ed::PinId(id * 10 + 0), "Value", valueType, true}};
            node.Outputs = {{ed::PinId(id * 10 + 1), "Result", valueType, false}};
            break;
        case NodeSubType::Length:
            node.Name = "Length " + typeName;
            node.Inputs = {{ed::PinId(id * 10 + 0), "Vector", valueType, true}};
            node.Outputs = {{ed::PinId(id * 10 + 1), "Length", PinType::F1, false}};
            break;
        case NodeSubType::Distance:
            node.Name = "Distance " + typeName;
            node.Inputs = {
                {ed::PinId(id * 10 + 0), "A", valueType, true},
                {ed::PinId(id * 10 + 1), "B", valueType, true}
            };
            node.Outputs = {{ed::PinId(id * 10 + 2), "Distance", PinType::F1, false}};
            break;
        case NodeSubType::Lerp:
            node.Name = "Lerp " + typeName;
            node.Inputs = {
                {ed::PinId(id * 10 + 0), "A", valueType, true},
                {ed::PinId(id * 10 + 1), "B", valueType, true},
                {ed::PinId(id * 10 + 2), "T", PinType::F1, true}
            };
            node.Outputs = {{ed::PinId(id * 10 + 3), "Result", valueType, false}};
            break;
        case NodeSubType::Clamp:
            node.Name = "Clamp " + typeName;
            node.Inputs = {
                {ed::PinId(id * 10 + 0), "Value", valueType, true},
                {ed::PinId(id * 10 + 1), "Min", valueType, true},
                {ed::PinId(id * 10 + 2), "Max", valueType, true}
            };
            node.Outputs = {{ed::PinId(id * 10 + 3), "Result", valueType, false}};
            break;
        case NodeSubType::And:
            node.Name = "And";
            node.Inputs = {
                {ed::PinId(id * 10 + 0), "A", PinType::Bool, true},
                {ed::PinId(id * 10 + 1), "B", PinType::Bool, true}
            };
            node.Outputs = {{ed::PinId(id * 10 + 2), "Result", PinType::Bool, false}};
            break;
        case NodeSubType::Or:
            node.Name = "Or";
            node.Inputs = {
                {ed::PinId(id * 10 + 0), "A", PinType::Bool, true},
                {ed::PinId(id * 10 + 1), "B", PinType::Bool, true}
            };
            node.Outputs = {{ed::PinId(id * 10 + 2), "Result", PinType::Bool, false}};
            break;
        default:
            node.Name = "Unknown";
            break;
    }

    return node;
}

AnimationGraph::Node AnimationGraph::CreateConstantFloatNode(int id, float value) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Float";
    node.Type = NodeType::Constant;
    node.SubType = NodeSubType::None;
    node.Inputs = {};
    node.Outputs = {{ed::PinId(id * 10 + 0), "Value", PinType::F1, false}};
    node.Value = value;
    return node;
}

AnimationGraph::Node AnimationGraph::CreateConstantVec2Node(int id, glm::vec2 value) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Vec2";
    node.Type = NodeType::Constant;
    node.SubType = NodeSubType::None;
    node.Inputs = {};
    node.Outputs = {{ed::PinId(id * 10 + 0), "Value", PinType::F2, false}};
    node.Value = value;
    return node;
}

AnimationGraph::Node AnimationGraph::CreateConstantVec3Node(int id, glm::vec3 value) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Vec3";
    node.Type = NodeType::Constant;
    node.SubType = NodeSubType::None;
    node.Inputs = {};
    node.Outputs = {{ed::PinId(id * 10 + 0), "Value", PinType::F3, false}};
    node.Value = value;
    return node;
}

AnimationGraph::Node AnimationGraph::CreateConstantVec4Node(int id, glm::vec4 value) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Vec4";
    node.Type = NodeType::Constant;
    node.SubType = NodeSubType::None;
    node.Inputs = {};
    node.Outputs = {{ed::PinId(id * 10 + 0), "Value", PinType::F4, false}};
    node.Value = value;
    return node;
}

AnimationGraph::Node AnimationGraph::CreateConstantIntNode(int id, int value) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Int";
    node.Type = NodeType::Constant;
    node.SubType = NodeSubType::None;
    node.Inputs = {};
    node.Outputs = {{ed::PinId(id * 10 + 0), "Value", PinType::I1, false}};
    node.Value = value;
    return node;
}

AnimationGraph::Node AnimationGraph::CreateConstantBoolNode(int id, bool value) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Bool";
    node.Type = NodeType::Constant;
    node.SubType = NodeSubType::None;
    node.Inputs = {};
    node.Outputs = {{ed::PinId(id * 10 + 0), "Value", PinType::Bool, false}};
    node.Value = value ? 1 : 0;
    return node;
}

AnimationGraph::Node AnimationGraph::CreateConstantStringNode(int id, const std::string& value) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "String";
    node.Type = NodeType::Constant;
    node.SubType = NodeSubType::None;
    node.Inputs = {};
    node.Outputs = {{ed::PinId(id * 10 + 0), "Value", PinType::String, false}};
    node.Value = value;
    return node;
}

AnimationGraph::Node AnimationGraph::CreateIfNode(int id) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "If";
    node.Type = NodeType::Control;
    node.SubType = NodeSubType::If;
    node.Inputs = {
        {ed::PinId(id * 10 + 0), "Flow", PinType::Flow, true},
        {ed::PinId(id * 10 + 1), "Condition", PinType::Bool, true}
    };
    node.Outputs = {
        {ed::PinId(id * 10 + 2), "True", PinType::Flow, false},
        {ed::PinId(id * 10 + 3), "False", PinType::Flow, false}
    };
    return node;
}

AnimationGraph::Node AnimationGraph::CreateForNode(int id) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "For Loop";
    node.Type = NodeType::Control;
    node.SubType = NodeSubType::For;
    node.Inputs = {
        {ed::PinId(id * 10 + 0), "Flow", PinType::Flow, true},
        {ed::PinId(id * 10 + 1), "Start", PinType::I1, true},
        {ed::PinId(id * 10 + 2), "End", PinType::I1, true}
    };
    node.Outputs = {
        {ed::PinId(id * 10 + 3), "Loop Body", PinType::Flow, false},
        {ed::PinId(id * 10 + 4), "Index", PinType::I1, false},
        {ed::PinId(id * 10 + 5), "Completed", PinType::Flow, false}
    };
    return node;
}

AnimationGraph::Node AnimationGraph::CreateBranchNode(int id) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Branch";
    node.Type = NodeType::Control;
    node.SubType = NodeSubType::Branch;
    node.Inputs = {
        {ed::PinId(id * 10 + 0), "Flow", PinType::Flow, true},
        {ed::PinId(id * 10 + 1), "Condition", PinType::Bool, true}
    };
    node.Outputs = {
        {ed::PinId(id * 10 + 2), "True", PinType::Flow, false},
        {ed::PinId(id * 10 + 3), "False", PinType::Flow, false}
    };
    return node;
}

AnimationGraph::Node AnimationGraph::CreateBlackHoleDecomposerNode(int id) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Get BlackHole";
    node.Type = NodeType::Decomposer;
    node.SubType = NodeSubType::Blackhole;
    node.Inputs = {
        {ed::PinId(id * 10 + 0), "BlackHole", PinType::BlackHole, true}
    };
    node.Outputs = {
        {ed::PinId(id * 10 + 1), "Mass", PinType::F1, false},
        {ed::PinId(id * 10 + 2), "Position", PinType::F3, false},
        {ed::PinId(id * 10 + 3), "Show Disk", PinType::Bool, false},
        {ed::PinId(id * 10 + 4), "Disk Density", PinType::F1, false},
        {ed::PinId(id * 10 + 5), "Disk Size", PinType::F1, false},
        {ed::PinId(id * 10 + 6), "Disk Color", PinType::F3, false},
        {ed::PinId(id * 10 + 7), "Spin", PinType::F1, false},
        {ed::PinId(id * 10 + 8), "Spin Axis", PinType::F3, false}
    };
    return node;
}

AnimationGraph::Node AnimationGraph::CreateCameraDecomposerNode(int id) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Get Camera";
    node.Type = NodeType::Decomposer;
    node.SubType = NodeSubType::Camera;
    node.Inputs = {
        {ed::PinId(id * 10 + 0), "Camera", PinType::Camera, true}
    };
    node.Outputs = {
        {ed::PinId(id * 10 + 1), "Position", PinType::F3, false},
        {ed::PinId(id * 10 + 2), "Yaw", PinType::F1, false},
        {ed::PinId(id * 10 + 3), "Pitch", PinType::F1, false},
        {ed::PinId(id * 10 + 4), "FOV", PinType::F1, false},
        {ed::PinId(id * 10 + 5), "Front", PinType::F3, false},
        {ed::PinId(id * 10 + 6), "Up", PinType::F3, false}
    };
    return node;
}

AnimationGraph::Node AnimationGraph::CreateGetBlackHoleFromSceneNode(int id, int index) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Get BlackHole From Scene";
    node.Type = NodeType::Other;
    node.SubType = NodeSubType::Blackhole;
    node.SceneObjectIndex = index;
    node.Inputs = {};
    node.Outputs = {
        {ed::PinId(id * 10 + 0), "BlackHole", PinType::BlackHole, false}
    };
    return node;
}

AnimationGraph::Node AnimationGraph::CreateGetCameraFromSceneNode(int id) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Get Camera From Scene";
    node.Type = NodeType::Other;
    node.SubType = NodeSubType::Camera;
    node.SceneObjectIndex = 0;
    node.Inputs = {};
    node.Outputs = {
        {ed::PinId(id * 10 + 0), "Camera", PinType::Camera, false}
    };
    return node;
}

AnimationGraph::Node AnimationGraph::CreateBlackHoleSetterNode(int id) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Set BlackHole";
    node.Type = NodeType::Setter;
    node.SubType = NodeSubType::Blackhole;
    node.Inputs = {
        {ed::PinId(id * 10 + 0), "Flow", PinType::Flow, true},
        {ed::PinId(id * 10 + 1), "BlackHole", PinType::BlackHole, true},
        {ed::PinId(id * 10 + 2), "Mass", PinType::F1, true},
        {ed::PinId(id * 10 + 3), "Position", PinType::F3, true},
        {ed::PinId(id * 10 + 4), "Show Disk", PinType::Bool, true},
        {ed::PinId(id * 10 + 5), "Disk Density", PinType::F1, true},
        {ed::PinId(id * 10 + 6), "Disk Size", PinType::F1, true},
        {ed::PinId(id * 10 + 7), "Disk Color", PinType::F3, true},
        {ed::PinId(id * 10 + 8), "Spin", PinType::F1, true},
        {ed::PinId(id * 10 + 9), "Spin Axis", PinType::F3, true}
    };
    node.Outputs = {
        {ed::PinId(id * 10 + 10), "Flow", PinType::Flow, false},
        {ed::PinId(id * 10 + 11), "BlackHole", PinType::BlackHole, false}
    };
    return node;
}

AnimationGraph::Node AnimationGraph::CreateCameraSetterNode(int id) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Name = "Set Camera";
    node.Type = NodeType::Setter;
    node.SubType = NodeSubType::Camera;
    node.Inputs = {
        {ed::PinId(id * 10 + 0), "Flow", PinType::Flow, true},
        {ed::PinId(id * 10 + 1), "Camera", PinType::Camera, true},
        {ed::PinId(id * 10 + 2), "Position", PinType::F3, true},
        {ed::PinId(id * 10 + 3), "Yaw", PinType::F1, true},
        {ed::PinId(id * 10 + 4), "Pitch", PinType::F1, true},
        {ed::PinId(id * 10 + 5), "FOV", PinType::F1, true}
    };
    node.Outputs = {
        {ed::PinId(id * 10 + 6), "Flow", PinType::Flow, false},
        {ed::PinId(id * 10 + 7), "Camera", PinType::Camera, false}
    };
    return node;
}

AnimationGraph::Node AnimationGraph::CreateVariableGetNode(int id, PinType varType, const std::string& varName) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Type = NodeType::Variable;
    node.SubType = NodeSubType::VariableGet;
    node.VariableName = varName;

    std::string typeName;
    switch (varType) {
        case PinType::F1: typeName = "Float"; break;
        case PinType::F2: typeName = "Vec2"; break;
        case PinType::F3: typeName = "Vec3"; break;
        case PinType::F4: typeName = "Vec4"; break;
        case PinType::I1: typeName = "Int"; break;
        case PinType::Bool: typeName = "Bool"; break;
        case PinType::String: typeName = "String"; break;
        case PinType::BlackHole: typeName = "BlackHole"; break;
        case PinType::Camera: typeName = "Camera"; break;
        default: typeName = "Any"; break;
    }

    node.Name = "Get " + typeName + " Variable";
    node.Inputs = {};
    node.Outputs = {{ed::PinId(id * 10 + 0), "Value", varType, false}};
    return node;
}

AnimationGraph::Node AnimationGraph::CreateVariableSetNode(int id, PinType varType, const std::string& varName) {
    Node node;
    node.Id = ed::NodeId(id);
    node.Type = NodeType::Variable;
    node.SubType = NodeSubType::VariableSet;
    node.VariableName = varName;

    std::string typeName;
    switch (varType) {
        case PinType::F1: typeName = "Float"; break;
        case PinType::F2: typeName = "Vec2"; break;
        case PinType::F3: typeName = "Vec3"; break;
        case PinType::F4: typeName = "Vec4"; break;
        case PinType::I1: typeName = "Int"; break;
        case PinType::Bool: typeName = "Bool"; break;
        case PinType::String: typeName = "String"; break;
        case PinType::BlackHole: typeName = "BlackHole"; break;
        case PinType::Camera: typeName = "Camera"; break;
        default: typeName = "Any"; break;
    }

    node.Name = "Set " + typeName + " Variable";
    node.Inputs = {
        {ed::PinId(id * 10 + 0), "Flow", PinType::Flow, true},
        {ed::PinId(id * 10 + 1), "Value", varType, true}
    };
    node.Outputs = {{ed::PinId(id * 10 + 2), "Flow", PinType::Flow, false}};
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

        // Serialize variable name if present
        if (!node.VariableName.empty()) {
            out << YAML::Key << "variable_name" << YAML::Value << node.VariableName;
        }

        // Serialize scene object index if set
        if (node.SceneObjectIndex >= 0) {
            out << YAML::Key << "scene_object_index" << YAML::Value << node.SceneObjectIndex;
        }

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
        } else if (std::holds_alternative<glm::vec2>(node.Value)) {
            auto v = std::get<glm::vec2>(node.Value);
            out << YAML::Key << "value_vec2" << YAML::Value << YAML::Flow << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
        } else if (std::holds_alternative<glm::vec3>(node.Value)) {
            auto v = std::get<glm::vec3>(node.Value);
            out << YAML::Key << "value_vec3" << YAML::Value << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
        } else if (std::holds_alternative<glm::vec4>(node.Value)) {
            auto v = std::get<glm::vec4>(node.Value);
            out << YAML::Key << "value_vec4" << YAML::Value << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << v.w << YAML::EndSeq;
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

            // Deserialize variable name
            if (n["variable_name"]) {
                nodeObj.VariableName = n["variable_name"].as<std::string>();
            }

            // Deserialize scene object index
            if (n["scene_object_index"]) {
                nodeObj.SceneObjectIndex = n["scene_object_index"].as<int>();
            }

            // Deserialize value
            if (n["value_string"]) nodeObj.Value = n["value_string"].as<std::string>();
            else if (n["value_float"]) nodeObj.Value = n["value_float"].as<float>();
            else if (n["value_int"]) nodeObj.Value = n["value_int"].as<int>();
            else if (n["value_vec2"]) {
                auto vec = n["value_vec2"];
                nodeObj.Value = glm::vec2(vec[0].as<float>(), vec[1].as<float>());
            }
            else if (n["value_vec3"]) {
                auto vec = n["value_vec3"];
                nodeObj.Value = glm::vec3(vec[0].as<float>(), vec[1].as<float>(), vec[2].as<float>());
            }
            else if (n["value_vec4"]) {
                auto vec = n["value_vec4"];
                nodeObj.Value = glm::vec4(vec[0].as<float>(), vec[1].as<float>(), vec[2].as<float>(), vec[3].as<float>());
            }

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
