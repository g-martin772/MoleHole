//
// Created by leo on 11/28/25.
//

#include "AnimationGraphWindow.h"
#include "../Application/UI.h"
#include "../Simulation/Scene.h"
#include "imgui.h"
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace AnimationGraphWindow {

void Render(UI* ui, Scene* scene) {
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    
    if (!ImGui::Begin("Animation Graph")) {
        ImGui::End();
        return;
    }

    auto* animGraph = ui->GetAnimationGraph();
    if (animGraph) {
        animGraph->UpdateSceneObjects(scene);

        ed::NodeId selectedNodeId = 0;

        ed::SetCurrentEditor(animGraph->GetEditorContext());
        ed::NodeId selectedNodes[1];
        int selectedCount = ed::GetSelectedNodes(selectedNodes, 1);
        if (selectedCount > 0) {
            selectedNodeId = selectedNodes[0];
        }
        ed::SetCurrentEditor(nullptr);

        ImVec2 windowSize = ImGui::GetContentRegionAvail();
        float inspectorWidth = 250.0f;

        ImGui::BeginChild("NodeEditorPanel", ImVec2(windowSize.x - inspectorWidth - 8.0f, 0), true);
        animGraph->Render();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("NodeInspectorPanel", ImVec2(inspectorWidth, 0), true);
        ImGui::Text("Node Inspector");
        ImGui::Separator();
        ImGui::Spacing();

        if (selectedNodeId) {
            animGraph->RenderNodeInspector(selectedNodeId);
        } else {
            ImGui::TextDisabled("Select a node to edit");
        }

        ImGui::EndChild();
    }

    ImGui::End();
}

} // namespace AnimationGraphWindow
