//
// Created by leo on 3/4/26.
//

#include "ScienceWindow.h"

#include "imgui.h"

void RenderSectionHeader(const char* str)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
    ImGui::Text("%s", str);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

void ScienceWindow::Render(UI* ui)
{
    if (!ImGui::Begin("Science")) {
        ImGui::End();
        return;
    }

    RenderSectionHeader("Science");

    ImGui::End();
}
