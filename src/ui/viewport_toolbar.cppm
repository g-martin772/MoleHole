export module MoleHole:UI.ViewportToolbar;

import std;
import imgui;

export namespace MoleHole::ViewportToolbar
{
    constexpr float kMargin = 16.0f;
    constexpr float kPadding = 10.0f;
    constexpr float kRounding = 8.0f;
    constexpr float kButtonHeight = 34.0f;
    constexpr float kIconWidth = 38.0f;
    constexpr float kTextPadding = 16.0f;

    constexpr ImVec4 kBackground{0.08f, 0.08f, 0.08f, 0.85f};
    constexpr ImVec4 kAccent{180.0f / 255.0f, 100.0f / 255.0f, 40.0f / 255.0f, 1.0f};
    constexpr ImVec4 kAccentHover{200.0f / 255.0f, 120.0f / 255.0f, 50.0f / 255.0f, 1.0f};
    constexpr ImVec4 kIdle{0.16f, 0.16f, 0.16f, 1.0f};
    constexpr ImVec4 kIdleHover{0.26f, 0.26f, 0.26f, 1.0f};

    [[nodiscard]] constexpr float Top(const float viewportMinY) noexcept { return viewportMinY + kMargin; }
    [[nodiscard]] constexpr float Height() noexcept { return kButtonHeight + 2.0f * kPadding; }

    [[nodiscard]] inline float TextButtonWidth(const std::string_view label)
    {
        const std::string text(label);
        return ImGui::CalcTextSize(text.c_str()).x + kTextPadding;
    }

    [[nodiscard]] inline float BarWidth(const float contentWidth) noexcept { return contentWidth + 2.0f * kPadding; }

    inline bool Begin(const char* id, const ImVec2 position)
    {
        ImGui::SetNextWindowPos(position);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPadding, kPadding));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, kBackground);
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                           ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking |
                                           ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                           ImGuiWindowFlags_NoSavedSettings;
        return ImGui::Begin(id, nullptr, flags);
    }

    inline void End()
    {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }

    inline bool Button(const char* label, const float width, const bool active = false)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, active ? kAccent : kIdle);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? kAccentHover : kIdleHover);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        const bool pressed = ImGui::Button(label, ImVec2(width, kButtonHeight));
        ImGui::PopStyleColor(3);
        return pressed;
    }

    inline bool IconButton(ImFont* iconFont, const char* icon, const char* fallback, const bool accent)
    {
        if (iconFont) ImGui::PushFont(iconFont);
        const bool pressed = Button(iconFont ? icon : fallback, kIconWidth, accent);
        if (iconFont) ImGui::PopFont();
        return pressed;
    }
}
