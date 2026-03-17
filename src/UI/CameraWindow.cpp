//
// Created by leo on 11/28/25.
//

#include "CameraWindow.h"
#include "ParameterWidgets.h"
#include "UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"

namespace CameraWindow {

static void RenderSectionHeader(const char* title) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
    ImGui::Text("%s", title);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

static void TableNextColumnPreserveSpacing() {
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::NewLine();
}

static void Separate() {
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();
}

void Render(UI* ui, Scene* scene) {
    if (!ImGui::Begin("Camera")) {
        ImGui::End();
        return;
    }

    auto& renderer = Application::GetRenderer();
    auto& camera = renderer.camera;

    RenderSectionHeader("GENERAL SETTINGS");

    if (ImGui::BeginTable("CameraSpeedGrid", 2, ImGuiTableFlags_None)) {
        ImGui::TableNextRow();
        TableNextColumnPreserveSpacing();
        ParameterWidgets::RenderParameter(Params::CameraSpeed, ui, ParameterWidgets::WidgetStyle::Compact);
        TableNextColumnPreserveSpacing();
        ParameterWidgets::RenderParameter(Params::CameraMouseSensitivity, ui, ParameterWidgets::WidgetStyle::Compact);
        ImGui::EndTable();
    }
    Separate();

    RenderSectionHeader("POSITION");

    if (camera)
    {
        ParameterWidgets::RenderParameter(Params::CameraPosition, ui, ParameterWidgets::WidgetStyle::Compact);
        if (ImGui::BeginTable("CameraPositionGrid", 3, ImGuiTableFlags_None)) {
            ImGui::TableNextRow();
            TableNextColumnPreserveSpacing();
            ParameterWidgets::RenderParameter(Params::CameraYaw, ui, ParameterWidgets::WidgetStyle::Compact);
            TableNextColumnPreserveSpacing();
            ParameterWidgets::RenderParameter(Params::CameraPitch, ui, ParameterWidgets::WidgetStyle::Compact);
            TableNextColumnPreserveSpacing();
            ParameterWidgets::RenderParameter(Params::RenderingFOV, ui, ParameterWidgets::WidgetStyle::Compact);
            ImGui::EndTable();
        }
        Separate();

        if (ImGui::Button("Reset Camera Position", ImVec2(-1, 0))) {
            glm::vec3 resetPos(0.0f, 20.0f, 100.0f);
            camera->SetPosition(resetPos);
            camera->SetYawPitch(-90.0f, 0.0f);
            Application::Params().Set(Params::CameraPosition, resetPos);
            Application::Params().Set(Params::CameraFront, camera->GetFront());
            Application::Params().Set(Params::CameraUp, camera->GetUp());
            Application::Params().Set(Params::CameraPitch, 0.0f);
            Application::Params().Set(Params::CameraYaw, -90.0f);
            ui->MarkConfigDirty();
        }
    }
    Separate();

    RenderSectionHeader("CAMERA MODES");

    bool thirdPerson = Application::Params().Get(Params::RenderingThirdPerson, false);
    if (ImGui::Checkbox("Third Person Perspective", &thirdPerson)) {
        Application::Params().Set(Params::RenderingThirdPerson, thirdPerson);
        ui->MarkConfigDirty();
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Controls");

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
    if (ImGui::BeginTable("ControlsGrid", 2, ImGuiTableFlags_None)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Button("WASD: Move", ImVec2(-1, 0));
        ImGui::TableNextColumn();
        ImGui::Button("QE: Up/Down", ImVec2(-1, 0));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Button("Shift: Sprint", ImVec2(-1, 0));
        ImGui::TableNextColumn();
        ImGui::Button("Mouse: Look Around", ImVec2(-1, 0));
        ImGui::EndTable();
    }
    ImGui::PopStyleColor(3);

    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Follow Object");
    std::string currentObjectName = Application::Params().Get(Params::CameraObject, std::string("None"));

    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##FollowObject", currentObjectName.c_str())) {
        if (ImGui::Selectable("None", currentObjectName == "None")) {
            Application::Params().Set(Params::CameraObject, std::string("None"));
            ui->MarkConfigDirty();
        }
        if (scene) {
            for (size_t i = 0; i < scene->objects.size(); ++i) {
                if (!scene->objects[i].HasClass("Mesh")) continue;

                ParameterHandle nameHandle("Entity.Name");
                auto nameValue = scene->objects[i].GetParameter(nameHandle);
                if (!std::holds_alternative<std::string>(nameValue)) continue;

                std::string meshName = std::get<std::string>(nameValue);
                bool isSelected = (meshName == currentObjectName);
                if (ImGui::Selectable(meshName.c_str(), isSelected)) {
                    Application::Params().Set(Params::CameraObject, meshName);
                    ui->MarkConfigDirty();
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    if (ImGui::BeginTable("ThirdPersonGrid", 2, ImGuiTableFlags_None)) {
        ImGui::TableNextRow();
        TableNextColumnPreserveSpacing();
        ParameterWidgets::RenderParameter(Params::ThirdPersonDistance, ui, ParameterWidgets::WidgetStyle::Compact);
        TableNextColumnPreserveSpacing();
        ParameterWidgets::RenderParameter(Params::ThirdPersonHeight, ui, ParameterWidgets::WidgetStyle::Compact);
        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace CameraWindow

