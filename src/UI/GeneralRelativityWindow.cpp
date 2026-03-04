//
// Created by leo on 12/28/25.
//

#include "GeneralRelativityWindow.h"
#include "ParameterWidgets.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include "imgui.h"

bool GeneralRelativityWindow::s_showAdvancedSettings = false;

void GeneralRelativityWindow::Render(bool* p_open, UI* ui) {
    ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("General Relativity Settings", p_open)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Configure Kerr metric black hole physics and rendering parameters");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ParameterWidgets::RenderParameter(Params::GRKerrPhysicsEnabled, ui, ParameterWidgets::WidgetStyle::Detailed);

    ParameterWidgets::RenderParameterGroupWithFilter(ParameterGroup::GeneralRelativity, ui,
        [](const ParameterMetadata& meta) {
            return meta.id != Params::GRKerrPhysicsEnabled.m_Id;
        }, ParameterWidgets::WidgetStyle::Detailed);

    ImGui::End();
}

