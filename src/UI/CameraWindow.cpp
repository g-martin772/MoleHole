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

void Render(UI* ui, Scene* scene) {
    if (!ImGui::Begin("Camera")) {
        ImGui::End();
        return;
    }

    auto& renderer = Application::GetRenderer();
    auto& camera = renderer.camera;

    if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Use ParameterWidgets for camera speed and sensitivity
        ParameterWidgets::RenderParameter(Params::CameraSpeed, ui, ParameterWidgets::WidgetStyle::Standard);
        ParameterWidgets::RenderParameter(Params::CameraMouseSensitivity, ui, ParameterWidgets::WidgetStyle::Standard);

        ImGui::Separator();

        if (camera) {
            glm::vec3 position = camera->GetPosition();
            if (ImGui::DragFloat3("Camera Position", &position[0], 0.1f)) {
                camera->SetPosition(position);
                Application::Params().Set(Params::CameraPosition, position);
                Application::Params().Set(Params::CameraFront, camera->GetFront());
                Application::Params().Set(Params::CameraUp, camera->GetUp());
                Application::Params().Set(Params::CameraPitch, camera->GetPitch());
                Application::Params().Set(Params::CameraYaw, camera->GetYaw());
                ui->MarkConfigDirty();
            }

            float yaw = camera->GetYaw();
            float pitch = camera->GetPitch();
            bool angleChanged = false;

            if (ImGui::DragFloat("Yaw", &yaw, 0.5f, -180.0f, 180.0f)) {
                angleChanged = true;
            }

            if (ImGui::DragFloat("Pitch", &pitch, 0.5f, -89.0f, 89.0f)) {
                angleChanged = true;
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

            // Use ParameterWidgets for FOV
            ParameterWidgets::RenderParameter(Params::RenderingFOV, ui, ParameterWidgets::WidgetStyle::Standard);
            // Update camera FOV if changed
            camera->SetFov(Application::Params().Get(Params::RenderingFOV, 45.0f));

            if (ImGui::Button("Reset Camera Position")) {
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
        ImGui::Text("Controls:");
        ImGui::BulletText("WASD - Move");
        ImGui::BulletText("QE - Up/Down");
        ImGui::BulletText("Right Mouse - Look around");
    }

    if (ImGui::CollapsingHeader("Camera Utilities", ImGuiTreeNodeFlags_DefaultOpen)) {
        ParameterWidgets::RenderParameter(Params::RenderingThirdPerson, ui, ParameterWidgets::WidgetStyle::Standard);

        if (Application::Params().Get(Params::RenderingThirdPerson, false)) {
            ImGui::Indent();

            static int selectedCameraIndex = -1;
            std::string currentObjectName = Application::Params().Get(Params::CameraObject, std::string("None"));

            if (ImGui::BeginCombo("Camera Object", currentObjectName.c_str())) {
                for (size_t i = 0; i < scene->meshes.size(); ++i) {
                    bool isSelected = (scene->meshes[i].name == currentObjectName);
                    if (ImGui::Selectable(scene->meshes[i].name.c_str(), isSelected)) {
                        selectedCameraIndex = static_cast<int>(i);
                        Application::Params().Set(Params::CameraObject, scene->meshes[i].name);
                        ui->MarkConfigDirty();
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Separator();
            ImGui::Text("Third-Person Settings:");

            // Third-person distance control
            ParameterWidgets::RenderParameter(Params::ThirdPersonDistance, ui, ParameterWidgets::WidgetStyle::Standard);

            // Third-person height control
            ParameterWidgets::RenderParameter(Params::ThirdPersonHeight, ui, ParameterWidgets::WidgetStyle::Standard);

            ImGui::Unindent();
        }
    }

    ImGui::End();
}

} // namespace CameraWindow

