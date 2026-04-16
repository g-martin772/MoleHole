//
// Created by leo on 12/28/25.
//

#include "GeneralRelativityWindow.h"
#include "ParameterWidgets.h"
#include "LatexRenderer.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include "../Application/AppState.h"
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

    int metricType = AppState::Params().Get<int>(Params::GRMetricType, 0);
    const char* metricNames[] = { "Schwarzschild", "Kerr", "Reissner-Nordström", "Kerr-Newman" };
    if (ImGui::Combo("Metric Type", &metricType, metricNames, IM_ARRAYSIZE(metricNames))) {
        AppState::Params().Set<int>(Params::GRMetricType, metricType);
    }

    ImGui::Spacing();
    ImGui::Text("Metric Tensor (Boyer-Lindquist coordinates):");
    if (ImGui::BeginTable("MetricTensor", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
        const char* matrix[16];
        for (int i = 0; i < 16; ++i) matrix[i] = "0";

        if (metricType == 0) { // Schwarzschild
            matrix[0] = "-(1 - \\frac{r_s}{r})";
            matrix[5] = "\\frac{1}{1 - \\frac{r_s}{r}}";
            matrix[10] = "r^2";
            matrix[15] = "r^2 \\sin^2(\\theta)";
        } else if (metricType == 1) { // Kerr
            matrix[0] = "-(1 - \\frac{r_s r}{\\rho^2})";
            matrix[3] = matrix[12] = "-\\frac{r_s r a \\sin^2(\\theta)}{\\rho^2}";
            matrix[5] = "\\frac{\\rho^2}{\\Delta}";
            matrix[10] = "\\rho^2";
            matrix[15] = "\\left(r^2 + a^2 + \\frac{r_s r a^2 \\sin^2(\\theta)}{\\rho^2}\\right)\\sin^2(\\theta)";
        } else if (metricType == 2) { // Reissner-Nordström
            matrix[0] = "-f(r)";
            matrix[5] = "\\frac{1}{f(r)}";
            matrix[10] = "r^2";
            matrix[15] = "r^2 \\sin^2(\\theta)";
        } else if (metricType == 3) { // Kerr-Newman
            matrix[0] = "-\\frac{\\Delta - a^2 \\sin^2(\\theta)}{\\rho^2}";
            matrix[3] = matrix[12] = "-\\frac{a \\sin^2(\\theta)(r^2 + a^2 - \\Delta)}{\\rho^2}";
            matrix[5] = "\\frac{\\rho^2}{\\Delta}";
            matrix[10] = "\\rho^2";
            matrix[15] = "\\frac{(r^2 + a^2)^2 - a^2 \\Delta \\sin^2(\\theta)}{\\rho^2} \\sin^2(\\theta)";
        }

        for (int i = 0; i < 16; ++i) {
            ImGui::TableNextColumn();
            std::string latexStr = std::string("$") + matrix[i] + "$";
            const auto& tex = LatexRenderer::Instance().RenderLatex(latexStr, 150);
            if (tex.valid) {
                ImGui::Image((ImTextureID)(intptr_t)tex.textureID, ImVec2(tex.width, tex.height));
            } else {
                ImGui::TextWrapped("%s", matrix[i]);
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ParameterWidgets::RenderParameter(Params::GRKerrPhysicsEnabled, ui, ParameterWidgets::WidgetStyle::Detailed);

    ParameterWidgets::RenderParameterGroupWithFilter(ParameterGroup::GeneralRelativity, ui,
        [](const ParameterMetadata& meta) {
            return meta.id != Params::GRKerrPhysicsEnabled.m_Id && meta.id != Params::GRMetricType.m_Id;
        }, ParameterWidgets::WidgetStyle::Detailed);

    ImGui::End();
}
