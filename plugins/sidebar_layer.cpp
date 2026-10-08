import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    constexpr const char* kIconCamera = "\xef\x80\xb0"; // U+F030
    constexpr const char* kIconSystem = "\xef\x8b\x9b"; // U+F2DB (microchip)
    constexpr const char* kIconScene = "\xef\x80\xbe"; // U+F03E (image)
    constexpr const char* kIconDebug = "\xef\x86\x88"; // U+F188 (bug)
    constexpr const char* kIconHud = "\xef\x98\xa4"; // U+F624 (gauge)
    constexpr const char* kIconAtom = "\xef\x97\x92"; // U+F5D2 (general relativity)
    constexpr const char* kIconBrain = "\xef\x97\x9c"; // U+F5DC (science)
    constexpr const char* kIconAnimGraph = "\xef\x95\x82"; // U+F542 (project-diagram)
    constexpr const char* kIconSettings = "\xef\x80\x93"; // U+F013 (gear)

    constexpr ImVec4 kAccent(180.0f / 255.0f, 100.0f / 255.0f, 40.0f / 255.0f, 1.0f);
    constexpr ImVec4 kAccentHover(200.0f / 255.0f, 120.0f / 255.0f, 50.0f / 255.0f, 1.0f);
    constexpr ImVec4 kNeutral(0.16f, 0.16f, 0.16f, 1.0f);
    constexpr ImVec4 kNeutralHover(0.24f, 0.24f, 0.24f, 1.0f);

    struct SidebarButton
    {
        const char* icon;
        const char* fallbackLabel;
        const char* tooltip;
        bool* active;
    };

    struct SideBarLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, UiState>;

        SideBarLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_UiState(std::move(uiState))
        {
        }

        void OnAttach() override
        {
            EnsureIconFont(*m_UiState);
        }

        void OnUiRender() override
        {
            constexpr float sidebarWidth = 72.0f;
            constexpr float buttonHeight = 44.0f;

            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + ImGui::GetFrameHeight()));
            ImGui::SetNextWindowSize(ImVec2(sidebarWidth, viewport->Size.y - ImGui::GetFrameHeight()));

            viewport->WorkPos.x += sidebarWidth;
            viewport->WorkSize.x -= sidebarWidth;

            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking |
                                               ImGuiWindowFlags_NoSavedSettings;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.06f, 0.95f));

            if (ImGui::Begin("##Sidebar", nullptr, flags))
            {
                const SidebarButton buttons[] = {
                    {kIconCamera, "CAM", "Camera", &m_UiState->ShowCameraWindow},
                    {kIconSystem, "SYS", "System", &m_UiState->ShowSystemWindow},
                    {kIconScene, "SCN", "Scene", &m_UiState->ShowSceneWindow},
                    {kIconDebug, "DBG", "Debug", &m_UiState->ShowDebugWindow},
                    {kIconHud, "HUD", "Viewport HUD", &m_UiState->ShowViewportHud},
                    {kIconAtom, "GR", "General Relativity", &m_UiState->ShowGeneralRelativityWindow},
                    {kIconBrain, "SCI", "Science", &m_UiState->ShowScienceWindow},
                    {kIconAnimGraph, "ANM", "Animation Graph", &m_UiState->ShowAnimationGraphWindow},
                };

                for (const auto& button : buttons)
                {
                    RenderButton(button);
                    ImGui::Spacing();
                }

                const float settingsY = ImGui::GetWindowHeight() - buttonHeight - 12.0f;
                if (ImGui::GetCursorPosY() < settingsY) ImGui::SetCursorPosY(settingsY);
                RenderButton({kIconSettings, "SET", "Settings", &m_UiState->ShowSettingsWindow});
            }
            ImGui::End();

            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }

    private:
        void RenderButton(const SidebarButton& button) const
        {
            constexpr float buttonHeight = 44.0f;
            const bool active = *button.active;

            ImGui::PushStyleColor(ImGuiCol_Button, active ? kAccent : kNeutral);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? kAccentHover : kNeutralHover);

            const bool hasIconFont = m_UiState->IconFont != nullptr;
            if (hasIconFont) ImGui::PushFont(m_UiState->IconFont);
            if (ImGui::Button(hasIconFont ? button.icon : button.fallbackLabel, ImVec2(-1, buttonHeight)))
            {
                *button.active = !*button.active;
            }
            if (hasIconFont) ImGui::PopFont();

            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", button.tooltip);
        }

        std::shared_ptr<UiState> m_UiState;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(SideBarLayer)
