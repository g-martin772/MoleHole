//
// Created by leo on 11/28/25.
//

#include "RendererWindow.h"
#include "ParameterWidgets.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include "imgui.h"

namespace RendererWindow {

void Render(UI* ui, float fps) {
    if (!ImGui::Begin("System")) {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("System Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Text("Frame time: %.3f ms", 1000.0f / fps);
    }

    // Display Settings Section (using ParameterWidgets)
    ParameterWidgets::RenderParameterGroup(ParameterGroup::Window, ui,
                                          ParameterWidgets::WidgetStyle::Standard, true);

    ImGui::End();
}

} // namespace RendererWindow
