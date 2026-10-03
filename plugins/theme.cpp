import GPP;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;

namespace
{
    struct MoleHoleTheme final : public Theme
    {
        using Dependencies = std::tuple<Logger>;

        explicit MoleHoleTheme(const std::shared_ptr<Logger>& logger)
            : Theme(logger)
        {
        }

        void Apply(ImGuiStyle& style, ImGuiIO&) override
        {
            ImGui::StyleColorsDark(&style);

            style.Alpha = 1.0f;
            style.DisabledAlpha = 0.5f;
            style.WindowPadding = ImVec2(8.0f, 8.0f);
            style.WindowRounding = 0.0f;
            style.WindowBorderSize = 0.0f;
            style.WindowMinSize = ImVec2(32.0f, 32.0f);
            style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_Right;
            style.ChildRounding = 0.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 0.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(20.0f, 8.1f);
            style.FrameRounding = 2.0f;
            style.FrameBorderSize = 0.0f;
            style.ItemSpacing = ImVec2(3.0f, 3.0f);
            style.ItemInnerSpacing = ImVec2(3.0f, 8.0f);
            style.CellPadding = ImVec2(6.0f, 14.1f);
            style.IndentSpacing = 0.0f;
            style.ColumnsMinSpacing = 10.0f;
            style.ScrollbarSize = 10.0f;
            style.ScrollbarRounding = 2.0f;
            style.GrabMinSize = 12.1f;
            style.GrabRounding = 1.0f;
            style.TabRounding = 0.0f;
            style.TabBorderSize = 1.0f;
            style.TabBarBorderSize = 1.0f;
            style.SeparatorTextBorderSize = 1.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            style.Colors[ImGuiCol_Text] = ImVec4(0.98039216f, 0.98039216f, 0.98039216f, 1.0f);
            style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.49803922f, 0.49803922f, 0.49803922f, 1.0f);
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.09411765f, 0.09411765f, 0.09411765f, 1.0f);
            style.Colors[ImGuiCol_ChildBg] = ImVec4(0.15686275f, 0.15686275f, 0.15686275f, 1.0f);
            style.Colors[ImGuiCol_PopupBg] = ImVec4(0.09411765f, 0.09411765f, 0.09411765f, 1.0f);
            style.Colors[ImGuiCol_Border] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
            style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.09803922f);
            style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.15686275f);
            style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.0f, 0.0f, 0.0f, 0.047058824f);
            style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.10980392f);
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(1.0f, 1.0f, 1.0f, 0.39215687f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.47058824f);
            style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.0f, 0.0f, 0.0f, 0.09803922f);
            style.Colors[ImGuiCol_CheckMark] = ImVec4(180.0f / 255, 100.0f / 255, 40.0f / 255, 255.0f / 255);
            style.Colors[ImGuiCol_SliderGrab] = ImVec4(180.0f / 255, 100.0f / 255, 40.0f / 255, 255.0f / 255);
            style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.3137255f);
            style.Colors[ImGuiCol_Button] = ImVec4(180.0f / 255, 100.0f / 255, 40.0f / 255, 255.0f / 255);
            style.Colors[ImGuiCol_ButtonHovered] = ImVec4(200.0f / 255, 120.0f / 255, 50.0f / 255, 255.0f / 255);
            style.Colors[ImGuiCol_ButtonActive] = ImVec4(160.0f / 255, 90.0f / 255, 35.0f / 255, 255.0f / 255);
            style.Colors[ImGuiCol_Header] = ImVec4(180.0f / 255, 100.0f / 255, 40.0f / 255, 255.0f / 255);
            style.Colors[ImGuiCol_HeaderHovered] = ImVec4(200.0f / 255, 120.0f / 255, 50.0f / 255, 255.0f / 255);
            style.Colors[ImGuiCol_HeaderActive] = ImVec4(160.0f / 255, 90.0f / 255, 35.0f / 255, 255.0f / 255);
            style.Colors[ImGuiCol_Separator] = ImVec4(180.0f / 255, 100.0f / 255, 40.0f / 255, 255.0f / 255);
            style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(200.0f / 255, 120.0f / 255, 50.0f / 255, 255.0f / 255);
            style.Colors[ImGuiCol_SeparatorActive] = ImVec4(160.0f / 255, 90.0f / 255, 35.0f / 255, 255.0f / 255);
            style.Colors[ImGuiCol_ResizeGrip] = ImVec4(1.0f, 1.0f, 1.0f, 0.15686275f);
            style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.23529412f);
            style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.23529412f);
            style.Colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
            style.Colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
            style.Colors[ImGuiCol_TabActive] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
            style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
            style.Colors[ImGuiCol_DockingPreview] = ImVec4(180.0f / 255, 100.0f / 255, 40.0f / 255, 0.7f);
            style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.09411765f, 0.09411765f, 0.09411765f, 1.0f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
            style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
            style.Colors[ImGuiCol_PlotLines] = ImVec4(1.0f, 1.0f, 1.0f, 0.3529412f);
            style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_PlotHistogram] = ImVec4(1.0f, 1.0f, 1.0f, 0.3529412f);
            style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.15686275f, 0.15686275f, 0.15686275f, 1.0f);
            style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(1.0f, 1.0f, 1.0f, 0.3137255f);
            style.Colors[ImGuiCol_TableBorderLight] = ImVec4(1.0f, 1.0f, 1.0f, 0.19607843f);
            style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
            style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.019607844f);
            style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
            style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.16862746f, 0.23137255f, 0.5372549f, 1.0f);
            style.Colors[ImGuiCol_NavHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
            style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
            style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5647059f);

            m_Logger->Info("MoleHoleTheme applied");
        }
    };
}

GPP_DEFINE_HOT_RELOAD_THEME(MoleHoleTheme)
