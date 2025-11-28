//
// Created by leo on 11/28/25.
//

#include "SideBar.h"
#include "../Application/UI.h"
#include "IconsFontAwesome6.h"
#include "imgui.h"
#include <glm/glm.hpp>

namespace SideBar {

void Render(UI* ui) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    const float sidebarWidth = 80.0f;
    const float iconSize = 48.0f;
    const float padding = 16.0f;
    const float animSpeed = 8.0f;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(sidebarWidth, viewport->Size.y - ImGui::GetFrameHeight()));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, padding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, padding));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, 0.95f));

    if (ImGui::Begin("##Sidebar", nullptr, flags)) {
        // Structure: icon, tooltip, active state, index
        struct SidebarButton {
            const char* icon;
            const char* tooltip;
            bool* activePtr;
            int index;
        };

        SidebarButton buttons[] = {
            { ICON_FA_CHART_LINE, "Animation Graph", ui->GetShowAnimationGraphPtr(), 0 },
            { ICON_FA_MICROCHIP, "System", ui->GetShowSystemWindowPtr(), 1 },
            { ICON_FA_CUBES, "Simulation", ui->GetShowSimulationWindowPtr(), 2 },
            { ICON_FA_IMAGE, "Scene", ui->GetShowSceneWindowPtr(), 3 },
            { ICON_FA_GEAR, "Settings", ui->GetShowSettingsWindowPtr(), 4 }
        };

        int hoveredItem = -1;
        float* sidebarHoverAnim = ui->GetSidebarHoverAnim();

        for (int i = 0; i < 5; ++i) {
            ImGui::PushID(i);

            bool isActive = *buttons[i].activePtr;
            bool isHovered = false;

            // Calculate animation value
            float targetAnim = 0.0f;

            // Center the button horizontally
            ImGui::SetCursorPosX((sidebarWidth - iconSize) * 0.5f);
            
            ImVec2 buttonPos = ImGui::GetCursorScreenPos();
            ImVec2 buttonSize(iconSize, iconSize);

            // Check if mouse is over this button
            if (ImGui::IsMouseHoveringRect(buttonPos, ImVec2(buttonPos.x + buttonSize.x, buttonPos.y + buttonSize.y))) {
                isHovered = true;
                hoveredItem = i;
                targetAnim = 1.0f;
            }

            if (isActive) {
                targetAnim = 1.0f;
            }

            // Smooth animation
            float delta = io.DeltaTime * animSpeed;
            if (sidebarHoverAnim[i] < targetAnim) {
                sidebarHoverAnim[i] = glm::min(sidebarHoverAnim[i] + delta, targetAnim);
            } else if (sidebarHoverAnim[i] > targetAnim) {
                sidebarHoverAnim[i] = glm::max(sidebarHoverAnim[i] - delta, targetAnim);
            }

            // Calculate colors with animation
            float animValue = sidebarHoverAnim[i];
            ImVec4 bgColor = ImVec4(
                0.15f + animValue * 0.15f,
                0.15f + animValue * 0.15f,
                0.15f + animValue * 0.15f,
                0.0f + animValue * 1.0f
            );

            ImVec4 iconColor;
            if (isActive) {
                // Active color (darker orange accent)
                iconColor = ImVec4(
                    180.0f/255.0f,
                    100.0f/255.0f,
                    40.0f/255.0f,
                    1.0f
                );
            } else {
                // Inactive/hover color
                iconColor = ImVec4(
                    0.7f + animValue * 0.2f,
                    0.7f + animValue * 0.2f,
                    0.7f + animValue * 0.2f,
                    1.0f
                );
            }

            // Draw background with animation
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (animValue > 0.01f) {
                // Use smaller radius for active state, larger for hover only
                float roundness = isActive ? 4.0f : 8.0f;
                ImVec2 bgPos = ImVec2(buttonPos.x - 4, buttonPos.y - 4);
                ImVec2 bgSize = ImVec2(buttonPos.x + buttonSize.x + 4, buttonPos.y + buttonSize.y + 4);

                // Animated glow effect
                if (isHovered && !isActive) {
                    float glowSize = 2.0f * animValue;
                    ImVec4 glowColor = ImVec4(1.0f, 1.0f, 1.0f, 0.1f * animValue);
                    drawList->AddRectFilled(
                        ImVec2(bgPos.x - glowSize, bgPos.y - glowSize),
                        ImVec2(bgSize.x + glowSize, bgSize.y + glowSize),
                        ImGui::GetColorU32(glowColor),
                        roundness + glowSize
                    );
                }

                drawList->AddRectFilled(bgPos, bgSize, ImGui::GetColorU32(bgColor), roundness);

                // Active indicator line
                if (isActive) {
                    float lineWidth = 3.0f;
                    ImVec2 lineStart = ImVec2(viewport->Pos.x + 2, buttonPos.y + buttonSize.y * 0.2f);
                    ImVec2 lineEnd = ImVec2(viewport->Pos.x + 2, buttonPos.y + buttonSize.y * 0.8f);
                    drawList->AddRectFilled(
                        lineStart,
                        ImVec2(lineStart.x + lineWidth, lineEnd.y),
                        ImGui::GetColorU32(ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f)),
                        lineWidth * 0.5f
                    );
                }
            }

            // Draw icon button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, iconColor);

            if (ui->GetIconFont()) {
                ImGui::PushFont(ui->GetIconFont());
            }

            if (ImGui::Button(buttons[i].icon, buttonSize)) {
                *buttons[i].activePtr = !(*buttons[i].activePtr);
            }

            if (ui->GetIconFont()) {
                ImGui::PopFont();
            }

            ImGui::PopStyleColor(4);

            // Tooltip
            if (isHovered) {
                ImGui::SetNextWindowPos(ImVec2(buttonPos.x + sidebarWidth + 10, buttonPos.y), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.2f, 0.2f, 0.2f, 0.95f));
                ImGui::BeginTooltip();
                ImGui::Text("%s", buttons[i].tooltip);
                ImGui::EndTooltip();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
            
            ImGui::PopID();
        }

        ui->SetHoveredSidebarItem(hoveredItem);
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

} // namespace SideBar
