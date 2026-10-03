import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct SidebarButton
    {
        const char* label;
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

        void OnUiRender() override
        {
            constexpr float sidebarWidth = 56.0f;
            constexpr float buttonHeight = 40.0f;

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + ImGui::GetFrameHeight()));
            ImGui::SetNextWindowSize(ImVec2(sidebarWidth, viewport->Size.y - ImGui::GetFrameHeight()));

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
                    {"CAM", "Camera", &m_UiState->ShowCameraWindow},
                    {"SYS", "System", &m_UiState->ShowSystemWindow},
                    {"SCN", "Scene", &m_UiState->ShowSceneWindow},
                    {"DBG", "Debug", &m_UiState->ShowDebugWindow},
                    {"HUD", "Viewport HUD", &m_UiState->ShowViewportHud},
                };

                for (const auto& button : buttons)
                {
                    const bool active = *button.active;
                    if (active)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button,
                                              ImVec4(180.0f / 255.0f, 100.0f / 255.0f, 40.0f / 255.0f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                              ImVec4(200.0f / 255.0f, 120.0f / 255.0f, 50.0f / 255.0f, 1.0f));
                    }
                    if (ImGui::Button(button.label, ImVec2(-1, buttonHeight)))
                    {
                        *button.active = !*button.active;
                    }
                    if (active) ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", button.tooltip);
                    ImGui::Spacing();
                }

                const float settingsY = ImGui::GetWindowHeight() - buttonHeight - 12.0f;
                if (ImGui::GetCursorPosY() < settingsY) ImGui::SetCursorPosY(settingsY);
                if (ImGui::Button("SET", ImVec2(-1, buttonHeight)))
                {
                    m_UiState->ShowSettingsWindow = !m_UiState->ShowSettingsWindow;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Settings");
            }
            ImGui::End();

            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }

    private:
        std::shared_ptr<UiState> m_UiState;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(SideBarLayer)
