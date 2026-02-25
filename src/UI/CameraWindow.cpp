//
// Created by leo on 11/28/25.
//

#include "CameraWindow.h"
#include "ParameterWidgets.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include "imgui.h"

namespace CameraWindow {

static void RenderSectionHeader(const char* title) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
    ImGui::Text("%s", title);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

void Render(UI* ui, Scene* scene) {
    if (!ImGui::Begin("Camera")) {
        ImGui::End();
        return;
    }

    auto& renderer = Application::GetRenderer();
    auto& camera = renderer.camera;

    RenderSectionHeader("CAMERA CONTROLS");

    if (ImGui::BeginTable("CameraSpeedGrid", 2, ImGuiTableFlags_None)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Speed");
        float speed = Application::Params().Get(Params::CameraSpeed, 1.0f);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##Speed", &speed, 0.1f, 0.01f, 100.0f)) {
            Application::Params().Set(Params::CameraSpeed, speed);
            ui->MarkConfigDirty();
        }

        ImGui::TableNextColumn();
        ImGui::Text("Mouse Sensitivity");
        float sensitivity = Application::Params().Get(Params::CameraMouseSensitivity, 0.05f);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##Sensitivity", &sensitivity, 0.005f, 0.001f, 1.0f, "%.3f")) {
            Application::Params().Set(Params::CameraMouseSensitivity, sensitivity);
            ui->MarkConfigDirty();
        }

        ImGui::EndTable();
    }

    if (camera) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Camera Position");

        glm::vec3 position = camera->GetPosition();
        if (ImGui::BeginTable("PosGrid", 3, ImGuiTableFlags_None)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "X");
            ImGui::SetNextItemWidth(-1);
            bool posChanged = ImGui::DragFloat("##PosX", &position.x, 0.1f);
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Y");
            ImGui::SetNextItemWidth(-1);
            posChanged |= ImGui::DragFloat("##PosY", &position.y, 0.1f);
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Z");
            ImGui::SetNextItemWidth(-1);
            posChanged |= ImGui::DragFloat("##PosZ", &position.z, 0.1f);
            ImGui::EndTable();

            if (posChanged) {
                camera->SetPosition(position);
                Application::Params().Set(Params::CameraPosition, position);
                Application::Params().Set(Params::CameraFront, camera->GetFront());
                Application::Params().Set(Params::CameraUp, camera->GetUp());
                Application::Params().Set(Params::CameraPitch, camera->GetPitch());
                Application::Params().Set(Params::CameraYaw, camera->GetYaw());
                ui->MarkConfigDirty();
            }
        }

        float yaw = camera->GetYaw();
        float pitch = camera->GetPitch();
        float fov = Application::Params().Get(Params::RenderingFOV, 45.0f);
        bool angleChanged = false;

        if (ImGui::BeginTable("AngleGrid", 3, ImGuiTableFlags_None)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Yaw");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##Yaw", &yaw, 0.5f, -180.0f, 180.0f)) {
                angleChanged = true;
            }
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Pitch");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##Pitch", &pitch, 0.5f, -89.0f, 89.0f)) {
                angleChanged = true;
            }
            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "FOV");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##FOV", &fov, 0.5f, 1.0f, 179.0f)) {
                Application::Params().Set(Params::RenderingFOV, fov);
                camera->SetFov(fov);
                ui->MarkConfigDirty();
            }
            ImGui::EndTable();
        }

        if (angleChanged) {
            camera->SetYawPitch(yaw, pitch);
            Application::Params().Set(Params::CameraPosition, camera->GetPosition());
            Application::Params().Set(Params::CameraFront, camera->GetFront());
            Application::Params().Set(Params::CameraUp, camera->GetUp());
            Application::Params().Set(Params::CameraPitch, pitch);
            Application::Params().Set(Params::CameraYaw, yaw);
            ui->MarkConfigDirty();
        }

        ImGui::Spacing();
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

    ImGui::Separator();
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

    if (ImGui::BeginTable("TPGrid", 2, ImGuiTableFlags_None)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Third Person Height");
        float tpHeight = Application::Params().Get(Params::ThirdPersonHeight, 2.0f);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##TPHeight", &tpHeight, 0.1f, 0.0f, 100.0f)) {
            Application::Params().Set(Params::ThirdPersonHeight, tpHeight);
            ui->MarkConfigDirty();
        }

        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Third Person Distance");
        float tpDist = Application::Params().Get(Params::ThirdPersonDistance, 5.0f);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##TPDist", &tpDist, 0.1f, 0.0f, 1000.0f)) {
            Application::Params().Set(Params::ThirdPersonDistance, tpDist);
            ui->MarkConfigDirty();
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace CameraWindow

