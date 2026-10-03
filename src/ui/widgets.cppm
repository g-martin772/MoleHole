export module MoleHole:UI.Widgets;

import std;
import imgui;
import :UI.State;

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

    inline ImFont* EnsureIconFont(UiState& state)
    {
        if (!state.IconFont)
        {
            static const ImWchar iconRanges[] = {0xe000, 0xf8ff, 0};
            ImFontConfig config;
            config.FontDataOwnedByAtlas = true;
            state.IconFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(
                "font/fa-solid-900.ttf", 24.0f, &config, iconRanges);
        }
        return state.IconFont;
    }
}
