export module MoleHole:UI.Widgets;

import std;
import imgui;

export namespace MoleHole
{
    inline void SectionHeader(const char* title)
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(180.0f / 255.0f, 100.0f / 255.0f, 40.0f / 255.0f, 1.0f));
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
    }
}
