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
            style.DisabledAlpha = 0.45f;
            style.WindowPadding = ImVec2(10.0f, 10.0f);
            style.WindowRounding = 4.0f;
            style.WindowBorderSize = 1.0f;
            style.WindowMinSize = ImVec2(32.0f, 32.0f);
            style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
            style.WindowMenuButtonPosition = ImGuiDir_None;
            style.ChildRounding = 4.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 4.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(8.0f, 5.0f);
            style.FrameRounding = 3.0f;
            style.FrameBorderSize = 0.0f;
            style.ItemSpacing = ImVec2(6.0f, 5.0f);
            style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
            style.CellPadding = ImVec2(6.0f, 4.0f);
            style.IndentSpacing = 14.0f;
            style.ColumnsMinSpacing = 10.0f;
            style.ScrollbarSize = 11.0f;
            style.ScrollbarRounding = 6.0f;
            style.GrabMinSize = 10.0f;
            style.GrabRounding = 3.0f;
            style.TabRounding = 4.0f;
            style.TabBorderSize = 0.0f;
            style.TabBarBorderSize = 1.0f;
            style.SeparatorTextBorderSize = 1.0f;
            style.ColorButtonPosition = ImGuiDir_Right;
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

            const auto rgb = [](float r, float g, float b, float a = 1.0f)
            {
                return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
            };
            const ImVec4 accent = rgb(180, 100, 40);
            const ImVec4 accentHover = rgb(200, 120, 50);
            const ImVec4 accentActive = rgb(160, 90, 35);
            const ImVec4 accentDim = rgb(180, 100, 40, 0.35f);
            const ImVec4 panel = rgb(26, 26, 26);
            const ImVec4 raised = rgb(36, 36, 36);
            const ImVec4 control = rgb(46, 46, 46);
            const ImVec4 controlHover = rgb(58, 58, 58);
            const ImVec4 controlActive = rgb(68, 68, 68);
            const ImVec4 border = rgb(54, 54, 54);

            auto& c = style.Colors;
            c[ImGuiCol_Text] = rgb(236, 236, 236);
            c[ImGuiCol_TextDisabled] = rgb(120, 120, 120);
            c[ImGuiCol_WindowBg] = panel;
            c[ImGuiCol_ChildBg] = raised;
            c[ImGuiCol_PopupBg] = rgb(30, 30, 30, 0.98f);
            c[ImGuiCol_Border] = border;
            c[ImGuiCol_BorderShadow] = rgb(0, 0, 0, 0);
            c[ImGuiCol_FrameBg] = control;
            c[ImGuiCol_FrameBgHovered] = controlHover;
            c[ImGuiCol_FrameBgActive] = controlActive;
            c[ImGuiCol_MenuBarBg] = rgb(0, 0, 0, 0);
            c[ImGuiCol_ScrollbarBg] = rgb(0, 0, 0, 0);
            c[ImGuiCol_ScrollbarGrab] = rgb(80, 80, 80);
            c[ImGuiCol_ScrollbarGrabHovered] = rgb(110, 110, 110);
            c[ImGuiCol_ScrollbarGrabActive] = accent;
            c[ImGuiCol_CheckMark] = accentHover;
            c[ImGuiCol_SliderGrab] = accent;
            c[ImGuiCol_SliderGrabActive] = accentHover;
            c[ImGuiCol_Button] = control;
            c[ImGuiCol_ButtonHovered] = controlHover;
            c[ImGuiCol_ButtonActive] = accentActive;
            c[ImGuiCol_Header] = accentDim;
            c[ImGuiCol_HeaderHovered] = rgb(180, 100, 40, 0.55f);
            c[ImGuiCol_HeaderActive] = rgb(180, 100, 40, 0.75f);
            c[ImGuiCol_Separator] = border;
            c[ImGuiCol_SeparatorHovered] = accentHover;
            c[ImGuiCol_SeparatorActive] = accentActive;
            c[ImGuiCol_ResizeGrip] = rgb(255, 255, 255, 0.08f);
            c[ImGuiCol_ResizeGripHovered] = accentHover;
            c[ImGuiCol_ResizeGripActive] = accentActive;
            c[ImGuiCol_Tab] = rgb(22, 22, 22);
            c[ImGuiCol_TabHovered] = rgb(58, 58, 58);
            c[ImGuiCol_TabSelected] = rgb(40, 40, 40);
            c[ImGuiCol_TabSelectedOverline] = accent;
            c[ImGuiCol_TabDimmed] = rgb(20, 20, 20);
            c[ImGuiCol_TabDimmedSelected] = rgb(32, 32, 32);
            c[ImGuiCol_TabDimmedSelectedOverline] = rgb(110, 110, 110);
            c[ImGuiCol_DockingPreview] = rgb(180, 100, 40, 0.6f);
            c[ImGuiCol_DockingEmptyBg] = rgb(16, 16, 16);
            c[ImGuiCol_TitleBg] = rgb(22, 22, 22);
            c[ImGuiCol_TitleBgActive] = rgb(30, 30, 30);
            c[ImGuiCol_TitleBgCollapsed] = rgb(22, 22, 22);
            c[ImGuiCol_PlotLines] = rgb(160, 160, 160);
            c[ImGuiCol_PlotLinesHovered] = accentHover;
            c[ImGuiCol_PlotHistogram] = accent;
            c[ImGuiCol_PlotHistogramHovered] = accentHover;
            c[ImGuiCol_TableHeaderBg] = raised;
            c[ImGuiCol_TableBorderStrong] = border;
            c[ImGuiCol_TableBorderLight] = rgb(44, 44, 44);
            c[ImGuiCol_TableRowBg] = rgb(0, 0, 0, 0);
            c[ImGuiCol_TableRowBgAlt] = rgb(255, 255, 255, 0.025f);
            c[ImGuiCol_TextSelectedBg] = rgb(180, 100, 40, 0.4f);
            c[ImGuiCol_DragDropTarget] = accentHover;
            c[ImGuiCol_NavCursor] = accentHover;
            c[ImGuiCol_NavWindowingHighlight] = rgb(255, 255, 255, 0.7f);
            c[ImGuiCol_NavWindowingDimBg] = rgb(0, 0, 0, 0.4f);
            c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 0.55f);

            m_Logger->Info("MoleHoleTheme applied");
        }
    };
}

GPP_DEFINE_HOT_RELOAD_THEME(MoleHoleTheme)
