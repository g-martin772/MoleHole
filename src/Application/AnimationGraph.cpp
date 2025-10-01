#include "AnimationGraph.h"
#include <imgui.h>
#include <algorithm>

namespace ed = ax::NodeEditor;

AnimationGraph::AnimationGraph()
    : m_Context(nullptr, ed::DestroyEditor), m_NextId(1)
{
    m_Context.reset(ed::CreateEditor());
    // Create 2 demo nodes with 1 input and 1 output each
    for (int i = 0; i < 2; ++i) {
        m_NodeIds.push_back(m_NextId++);
        m_InputPins.push_back(m_NextId++);
        m_OutputPins.push_back(m_NextId++);
    }
}

AnimationGraph::~AnimationGraph() = default;

void AnimationGraph::Render() {
    ed::SetCurrentEditor(m_Context.get());
    ed::Begin("Node Editor");

    for (int i = 0; i < 2; ++i) {
        ed::BeginNode(m_NodeIds[i]);
        ImGui::Text("Node %d", i+1);
        ed::BeginPin(m_InputPins[i], ed::PinKind::Input);
        ImGui::Text("In");
        ed::EndPin();
        ImGui::SameLine();
        ed::BeginPin(m_OutputPins[i], ed::PinKind::Output);
        ImGui::Text("Out");
        ed::EndPin();
        ed::EndNode();
    }

    // Draw existing links
    for (const auto& link : m_Links) {
        ed::Link(link.first, link.second.first, link.second.second);
    }

    // Handle new links
    ed::PinId inputPinId, outputPinId;
    if (ed::BeginCreate()) {
        if (ed::QueryNewLink(&inputPinId, &outputPinId)) {
            if (inputPinId && outputPinId && inputPinId != outputPinId) {
                if (ed::AcceptNewItem()) {
                    ed::LinkId newLinkId = m_NextId++;
                    m_Links.emplace_back(newLinkId, std::make_pair(inputPinId, outputPinId));
                }
            }
        }
    }
    ed::EndCreate();

    // Handle deleting links
    ed::LinkId linkId;
    if (ed::BeginDelete()) {
        if (ed::QueryDeletedLink(&linkId)) {
            if (ed::AcceptDeletedItem()) {
                m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
                    [linkId](const auto& l) { return l.first == linkId; }), m_Links.end());
            }
        }
    }
    ed::EndDelete();

    ed::End();
    ed::SetCurrentEditor(nullptr);
}
