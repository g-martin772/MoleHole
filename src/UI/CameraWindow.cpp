//
// Created by leo on 11/28/25.
//

#include "CameraWindow.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "imgui.h"

namespace CameraWindow {

void Render(UI* ui) {
    if (!ImGui::Begin("Camera")) {
        ImGui::End();
        return;
    }

    auto& renderer = Application::GetRenderer();
    auto& camera = renderer.camera;

    if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        float cameraSpeed = Application::State().app.cameraSpeed;
        if (ImGui::DragFloat("Movement Speed", &cameraSpeed, 0.1f, 0.1f, 50.0f)) {
            Application::State().app.cameraSpeed = cameraSpeed;
            ui->MarkConfigDirty();
        }

        float mouseSensitivity = Application::State().app.mouseSensitivity;
        if (ImGui::DragFloat("Mouse Sensitivity", &mouseSensitivity, 0.01f, 0.01f, 5.0f)) {
            Application::State().app.mouseSensitivity = mouseSensitivity;
            ui->MarkConfigDirty();
        }

        ImGui::Separator();

        if (camera) {
            glm::vec3 position = camera->GetPosition();
            if (ImGui::DragFloat3("Camera Position", &position[0], 0.1f)) {
                camera->SetPosition(position);
                Application::State().UpdateCameraState(position, camera->GetFront(), camera->GetUp(), camera->GetPitch(), camera->GetYaw());
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
                Application::State().UpdateCameraState(camera->GetPosition(), camera->GetFront(), camera->GetUp(), pitch, yaw);
                ui->MarkConfigDirty();
            }

            float fov = Application::State().rendering.fov;
            if (ImGui::DragFloat("Field of View", &fov, 0.5f, 10.0f, 120.0f)) {
                Application::State().rendering.fov = fov;
                camera->SetFov(fov);
                ui->MarkConfigDirty();
            }

            if (ImGui::Button("Reset Camera Position")) {
                glm::vec3 resetPos(0.0f, 20.0f, 100.0f);
                camera->SetPosition(resetPos);
                camera->SetYawPitch(-90.0f, 0.0f);
                Application::State().UpdateCameraState(resetPos, camera->GetFront(), camera->GetUp(), 0.0f, -90.0f);
                ui->MarkConfigDirty();
            }
        }

        ImGui::Separator();
        ImGui::Text("Controls:");
        ImGui::BulletText("WASD - Move");
        ImGui::BulletText("QE - Up/Down");
        ImGui::BulletText("Right Mouse - Look around");
    }

    ImGui::End();
}

} // namespace CameraWindow

