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

    const float sidebarWidth = 60.0f;
    const float iconSize = 64.0f;
    const float padding = 10.0f;
    const float animSpeed = 8.0f;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(sidebarWidth, viewport->Size.y - ImGui::GetFrameHeight()));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, padding));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.06f, 0.95f));

    if (ImGui::Begin("##Sidebar", nullptr, flags)) {
        struct SidebarButton {
            const char* icon;
            const char* tooltip;
            bool* activePtr;
            int index;
        };

        SidebarButton topButtons[] = {
            { ICON_FA_CAMERA, "Camera", ui->GetShowCameraWindowPtr(), 0 },
            { ICON_FA_MICROCHIP, "System", ui->GetShowSystemWindowPtr(), 1 },
            { ICON_FA_IMAGE, "Scene", ui->GetShowSceneWindowPtr(), 2 },
            { ICON_FA_BUG, "Debug", ui->GetShowDebugWindowPtr(), 3 },
            { ICON_FA_SIM, "Simulation", ui->GetShowSimulationWindowPtr(), 4 },
            { ICON_FA_CHART_LINE, "Animation Graph", ui->GetShowAnimationGraphPtr(), 5 },
            { ICON_FA_ATOM, "General Relativity", ui->GetShowGeneralRelativityWindowPtr(), 6 },
            { ICON_FA_BRAIN, "Science", ui->GetShowScienceWindowPtr(), 7 },
            { ICON_FA_GAUGE, "Viewport HUD", ui->GetShowViewportHUDPtr(), 8 }
        };

        SidebarButton settingsButton = { ICON_FA_GEAR, "Settings", ui->GetShowSettingsWindowPtr(), 8 };

        int hoveredItem = -1;
        float* sidebarHoverAnim = ui->GetSidebarHoverAnim();

        ImGui::SetCursorPosY(12.0f);

        for (int i = 0; i < 9; ++i) {
            ImGui::PushID(i);

            bool isActive = *topButtons[i].activePtr;
            bool isHovered = false;

            ImGui::SetCursorPosX((sidebarWidth - iconSize) * 0.5f);

            ImVec2 buttonPos = ImGui::GetCursorScreenPos();
            ImVec2 buttonSize(iconSize, iconSize - 10.0f);

            if (ImGui::IsMouseHoveringRect(buttonPos, ImVec2(buttonPos.x + buttonSize.x, buttonPos.y + buttonSize.y))) {
                isHovered = true;
                hoveredItem = i;
            }

            float targetAnim = (isActive || isHovered) ? 1.0f : 0.0f;

            float delta = io.DeltaTime * animSpeed;
            if (sidebarHoverAnim[i] < targetAnim) {
                sidebarHoverAnim[i] = glm::min(sidebarHoverAnim[i] + delta, targetAnim);
            } else if (sidebarHoverAnim[i] > targetAnim) {
                sidebarHoverAnim[i] = glm::max(sidebarHoverAnim[i] - delta, targetAnim);
            }

            float animValue = sidebarHoverAnim[i];
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            if (isActive) {
                ImVec2 bgPos = ImVec2(buttonPos.x - 2, buttonPos.y - 2);
                ImVec2 bgEnd = ImVec2(buttonPos.x + buttonSize.x + 2, buttonPos.y + buttonSize.y + 2);
                drawList->AddRectFilled(bgPos, bgEnd,
                    ImGui::GetColorU32(ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f)), 6.0f);
            } else if (animValue > 0.01f) {
                ImVec2 bgPos = ImVec2(buttonPos.x - 2, buttonPos.y - 2);
                ImVec2 bgEnd = ImVec2(buttonPos.x + buttonSize.x + 2, buttonPos.y + buttonSize.y + 2);
                drawList->AddRectFilled(bgPos, bgEnd,
                    ImGui::GetColorU32(ImVec4(0.25f, 0.25f, 0.25f, animValue)), 6.0f);
            }

            ImVec4 iconColor;
            if (isActive) {
                iconColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            } else {
                iconColor = ImVec4(
                    0.55f + animValue * 0.25f,
                    0.55f + animValue * 0.25f,
                    0.55f + animValue * 0.25f,
                    1.0f
                );
            }

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, iconColor);

            if (ui->GetIconFont()) {
                ImGui::PushFont(ui->GetIconFont());
            }

            if (ImGui::Button(topButtons[i].icon, buttonSize)) {
                *topButtons[i].activePtr = !(*topButtons[i].activePtr);
            }

            if (ui->GetIconFont()) {
                ImGui::PopFont();
            }

            ImGui::PopStyleColor(4);

            if (isHovered) {
                ImGui::SetNextWindowPos(ImVec2(buttonPos.x + sidebarWidth + 10, buttonPos.y), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.2f, 0.2f, 0.2f, 0.95f));
                ImGui::BeginTooltip();
                ImGui::Text("%s", topButtons[i].tooltip);
                ImGui::EndTooltip();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }

            ImGui::PopID();
        }

        float windowHeight = ImGui::GetWindowHeight();
        float settingsY = windowHeight - iconSize - 12.0f;
        if (ImGui::GetCursorPosY() < settingsY) {
            ImGui::SetCursorPosY(settingsY);
        }

        {
            int i = 8;
            ImGui::PushID(i);

            bool isActive = *settingsButton.activePtr;
            bool isHovered = false;

            ImGui::SetCursorPosX((sidebarWidth - iconSize) * 0.5f);

            ImVec2 buttonPos = ImGui::GetCursorScreenPos();
            ImVec2 buttonSize(iconSize, iconSize - 10.0f);

            if (ImGui::IsMouseHoveringRect(buttonPos, ImVec2(buttonPos.x + buttonSize.x, buttonPos.y + buttonSize.y))) {
                isHovered = true;
                hoveredItem = i;
            }

            float targetAnim = (isActive || isHovered) ? 1.0f : 0.0f;

            float delta = io.DeltaTime * animSpeed;
            if (sidebarHoverAnim[i] < targetAnim) {
                sidebarHoverAnim[i] = glm::min(sidebarHoverAnim[i] + delta, targetAnim);
            } else if (sidebarHoverAnim[i] > targetAnim) {
                sidebarHoverAnim[i] = glm::max(sidebarHoverAnim[i] - delta, targetAnim);
            }

            float animValue = sidebarHoverAnim[i];
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            if (animValue > 0.01f && !isActive) {
                ImVec2 bgPos = ImVec2(buttonPos.x - 2, buttonPos.y - 2);
                ImVec2 bgEnd = ImVec2(buttonPos.x + buttonSize.x + 2, buttonPos.y + buttonSize.y + 2);
                drawList->AddRectFilled(bgPos, bgEnd,
                    ImGui::GetColorU32(ImVec4(0.25f, 0.25f, 0.25f, animValue)), 6.0f);
            }

            ImVec4 iconColor = ImVec4(
                0.55f + animValue * 0.25f,
                0.55f + animValue * 0.25f,
                0.55f + animValue * 0.25f,
                1.0f
            );

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, iconColor);

            if (ui->GetIconFont()) {
                ImGui::PushFont(ui->GetIconFont());
            }

            if (ImGui::Button(settingsButton.icon, buttonSize)) {
                *settingsButton.activePtr = !(*settingsButton.activePtr);
            }

            if (ui->GetIconFont()) {
                ImGui::PopFont();
            }

            ImGui::PopStyleColor(4);

            if (isHovered) {
                ImGui::SetNextWindowPos(ImVec2(buttonPos.x + sidebarWidth + 10, buttonPos.y), ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.2f, 0.2f, 0.2f, 0.95f));
                ImGui::BeginTooltip();
                ImGui::Text("%s", settingsButton.tooltip);
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
