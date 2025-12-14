//
// Created by leo on 11/28/25.
//

#include "SimulationWindow.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Simulation/Scene.h"
#include "IconsFontAwesome6.h"
#include "imgui.h"
#include "spdlog/spdlog.h"
#include <nfd.h>
#include <filesystem>
#include <cstring>

namespace SimulationWindow {

void RenderSimulationControls(UI* ui) {
    // Don't render controls when taking a screenshot
    if (ui->IsTakingScreenshot()) {
        return;
    }
    
    auto& simulation = Application::GetSimulation();
    auto& renderer = Application::GetRenderer();

    ImVec2 viewportPos = ImVec2(renderer.m_viewportX, renderer.m_viewportY);
    ImVec2 viewportSize = ImVec2(renderer.m_viewportWidth, renderer.m_viewportHeight);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.8f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking;

    ImGui::SetNextWindowPos(ImVec2(viewportPos.x + viewportSize.x * 0.5f, viewportPos.y + 8), ImGuiCond_Always, ImVec2(0.5f, 0.0f));

    if (ImGui::Begin("##SimulationControls", nullptr, flags)) {
        constexpr float buttonSize = 64.0f;

        bool isStopped = simulation.IsStopped();
        bool isPaused = simulation.IsPaused();

        // Use icon font if available, otherwise fall back to text
        if (ui->GetIconFont()) {
            ImGui::PushFont(ui->GetIconFont());
        }

        if (isStopped || isPaused) {
            if (ImGui::Button(ui->GetIconFont() ? ICON_FA_PLAY : (isPaused ? "|>" : ">>"), ImVec2(buttonSize, buttonSize))) {
                simulation.Start();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(isPaused ? "Resume" : "Start");
            }
        } else {
            if (ImGui::Button(ui->GetIconFont() ? ICON_FA_PAUSE : "||", ImVec2(buttonSize, buttonSize))) {
                simulation.Pause();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Pause");
            }
        }

        ImGui::SameLine();

        if (ImGui::Button(ui->GetIconFont() ? ICON_FA_STOP : "[]", ImVec2(buttonSize, buttonSize))) {
            simulation.Stop();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Stop");
        }

        if (ui->GetIconFont()) {
            ImGui::PopFont();
        }

        if (!isStopped) {
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::Text("%.2fs", simulation.GetSimulationTime());
        }
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void Render(UI* ui, Scene* scene) {
    if (!ImGui::Begin("Simulation")) {
        ImGui::End();
        return;
    }

    // Black Holes Section
    if (ImGui::CollapsingHeader("Black Holes", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!scene) {
            ImGui::TextDisabled("No scene loaded");
        } else {
            // Transform Controls (only show if something is selected)
            if (scene->HasSelection()) {
                ImGui::Text("Transform Controls:");
                ImGui::SameLine();

                auto currentOp = ui->GetCurrentGizmoOperation();
                if (ImGui::RadioButton("Translate", currentOp == UI::GizmoOperation::Translate)) {
                    ui->SetCurrentGizmoOperation(UI::GizmoOperation::Translate);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Rotate", currentOp == UI::GizmoOperation::Rotate)) {
                    ui->SetCurrentGizmoOperation(UI::GizmoOperation::Rotate);
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Scale", currentOp == UI::GizmoOperation::Scale)) {
                    ui->SetCurrentGizmoOperation(UI::GizmoOperation::Scale);
                }

                bool useSnap = ui->IsUsingSnap();
                if (ImGui::Checkbox("Use Snap", &useSnap)) {
                    ui->SetUsingSnap(useSnap);
                }
                
                if (useSnap) {
                    switch (currentOp) {
                        case UI::GizmoOperation::Translate:
                            ImGui::DragFloat3("Snap", ui->GetSnapTranslate(), 0.1f);
                            break;
                        case UI::GizmoOperation::Rotate:
                            ImGui::DragFloat("Snap", ui->GetSnapRotatePtr(), 1.0f);
                            break;
                        case UI::GizmoOperation::Scale:
                            ImGui::DragFloat("Snap", ui->GetSnapScalePtr(), 0.01f);
                            break;
                    }
                }

                if (ImGui::Button("Deselect")) {
                    scene->ClearSelection();
                }

                ImGui::Separator();
            }

            if (ImGui::Button("Add Black Hole")) {
                BlackHole newHole;
                newHole.mass = 10.0f;
                newHole.position = glm::vec3(0.0f, 0.0f, -5.0f);
                newHole.showAccretionDisk = true;
                newHole.accretionDiskDensity = 1.0f;
                newHole.accretionDiskSize = 15.0f;
                newHole.accretionDiskColor = glm::vec3(1.0f, 0.5f, 0.0f);
                newHole.spinAxis = glm::vec3(0.0f, 1.0f, 0.0f);
                newHole.spin = 0.5f;
                scene->blackHoles.push_back(newHole);
            }

            ImGui::Text("Black Holes: %zu", scene->blackHoles.size());

            int idx = 0;
            for (auto it = scene->blackHoles.begin(); it != scene->blackHoles.end();) {
                ImGui::PushID(("bh_" + std::to_string(idx)).c_str());

                bool isSelected = scene->HasSelection() &&
                                 scene->selectedObject->type == Scene::ObjectType::BlackHole &&
                                 scene->selectedObject->index == static_cast<size_t>(idx);

                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.7f, 1.0f, 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.4f, 0.8f, 1.0f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.5f, 0.9f, 1.0f, 1.0f));
                }

                std::string label = "Black Hole #" + std::to_string(idx + 1);
                if (isSelected) {
                    label += " (Selected)";
                }

                bool open = ImGui::TreeNode(label.c_str());

                if (isSelected) {
                    ImGui::PopStyleColor(3);
                }

                ImGui::SameLine();
                
                // Styled Select button
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(200.0f/255.0f, 120.0f/255.0f, 50.0f/255.0f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(160.0f/255.0f, 90.0f/255.0f, 35.0f/255.0f, 1.0f));
                if (ImGui::SmallButton("Select")) {
                    scene->SelectObject(Scene::ObjectType::BlackHole, idx);
                }
                ImGui::PopStyleColor(3);
                
                ImGui::SameLine();
                
                // Styled Remove button
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                bool remove = ImGui::SmallButton("Remove");
                ImGui::PopStyleColor(3);

                if (open) {
                    BlackHole& bh = *it;
                    bool bhChanged = false;

                    if (ImGui::DragFloat("Mass", &bh.mass, 0.02f, 0.0f, 1e10f)) bhChanged = true;
                    if (ImGui::DragFloat("Spin", &bh.spin, 0.01f, 0.0f, 2.0f)) bhChanged = true;
                    if (ImGui::DragFloat3("Position", &bh.position[0], 0.05f)) bhChanged = true;
                    if (ImGui::DragFloat3("Spin Axis", &bh.spinAxis[0], 0.01f, -1.0f, 1.0f)) bhChanged = true;

                    if (bhChanged) {
                        if (!scene->currentPath.empty()) {
                            scene->Serialize(scene->currentPath);
                        }
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();

                if (remove) {
                    if (isSelected) {
                        scene->ClearSelection();
                    }
                    it = scene->blackHoles.erase(it);
                    if (!scene->currentPath.empty()) {
                        scene->Serialize(scene->currentPath);
                    }
                } else {
                    ++it;
                }
                idx++;
            }
        }
    }

    // Meshes Section
    if (ImGui::CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!scene) {
            ImGui::TextDisabled("No scene loaded");
        } else {
            if (ImGui::Button("Add Mesh")) {
                nfdchar_t* outPath = nullptr;
                nfdfilteritem_t filterItems[] = {
                    { "GLTF/GLB", "gltf,glb" }
                };
                nfdresult_t result = NFD_OpenDialog(&outPath, filterItems, 1, nullptr);

                if (result == NFD_OKAY && outPath) {
                    MeshObject newMesh;
                    newMesh.path = outPath;
                    newMesh.name = std::filesystem::path(outPath).stem().string();
                    newMesh.position = glm::vec3(0.0f, 0.0f, 0.0f);
                    newMesh.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                    newMesh.scale = glm::vec3(1.0f);
                    scene->meshes.push_back(newMesh);
                    free(outPath);

                    if (!scene->currentPath.empty()) {
                        scene->Serialize(scene->currentPath);
                    }
                }
            }

            ImGui::Text("Meshes: %zu", scene->meshes.size());

            int idx = 0;
            for (auto it = scene->meshes.begin(); it != scene->meshes.end();) {
                ImGui::PushID(("mesh_" + std::to_string(idx)).c_str());

                bool isSelected = scene->HasSelection() &&
                                 scene->selectedObject->type == Scene::ObjectType::Mesh &&
                                 scene->selectedObject->index == static_cast<size_t>(idx);

                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.7f, 1.0f, 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.4f, 0.8f, 1.0f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.5f, 0.9f, 1.0f, 1.0f));
                }

                std::string label = it->name.empty() ? "Mesh #" + std::to_string(idx + 1) : it->name;
                if (isSelected) {
                    label += " (Selected)";
                }

                bool open = ImGui::TreeNode(label.c_str());

                if (isSelected) {
                    ImGui::PopStyleColor(3);
                }

                ImGui::SameLine();
                
                // Styled Select button
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(200.0f/255.0f, 120.0f/255.0f, 50.0f/255.0f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(160.0f/255.0f, 90.0f/255.0f, 35.0f/255.0f, 1.0f));
                if (ImGui::SmallButton("Select")) {
                    scene->SelectObject(Scene::ObjectType::Mesh, idx);
                }
                ImGui::PopStyleColor(3);
                
                ImGui::SameLine();
                
                // Styled Remove button
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                bool remove = ImGui::SmallButton("Remove");
                ImGui::PopStyleColor(3);

                if (open) {
                    MeshObject& mesh = *it;
                    bool meshChanged = false;

                    char nameBuffer[128];
                    std::strncpy(nameBuffer, mesh.name.c_str(), sizeof(nameBuffer));
                    nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
                        mesh.name = nameBuffer;
                        meshChanged = true;
                    }

                    ImGui::TextWrapped("Path: %s", mesh.path.c_str());

                    ImGui::SameLine();
                    if (ImGui::Button("Change...")) {
                        nfdchar_t* outPath = nullptr;
                        nfdfilteritem_t filterItems[] = {
                            { "GLTF/GLB", "gltf,glb" }
                        };
                        nfdresult_t result = NFD_OpenDialog(&outPath, filterItems, 1, nullptr);

                        if (result == NFD_OKAY && outPath) {
                            mesh.path = outPath;
                            free(outPath);
                            meshChanged = true;
                        }
                    }

                    if (ImGui::DragFloat("Mass (kg)", &mesh.massKg, 100.0f, 0.1f, 1e10f)) {
                        meshChanged = true;
                    }

                    if (ImGui::DragFloat3("Position", &mesh.position[0], 0.1f)) {
                        meshChanged = true;
                    }

                    float eulerAngles[3];
                    glm::quat q = mesh.rotation;
                    eulerAngles[0] = glm::degrees(glm::pitch(q));
                    eulerAngles[1] = glm::degrees(glm::yaw(q));
                    eulerAngles[2] = glm::degrees(glm::roll(q));
                    
                    if (ImGui::DragFloat3("Rotation (deg)", eulerAngles, 1.0f)) {
                        q = glm::quat(glm::vec3(
                            glm::radians(eulerAngles[0]),
                            glm::radians(eulerAngles[1]),
                            glm::radians(eulerAngles[2])
                        ));
                        mesh.rotation = q;
                        meshChanged = true;
                    }

                    if (ImGui::DragFloat3("Scale", &mesh.scale[0], 0.01f)) {
                        meshChanged = true;
                    }

                    if (meshChanged && !scene->currentPath.empty()) {
                        scene->Serialize(scene->currentPath);
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();

                if (remove) {
                    if (isSelected) {
                        scene->ClearSelection();
                    }
                    it = scene->meshes.erase(it);
                    if (!scene->currentPath.empty()) {
                        scene->Serialize(scene->currentPath);
                    }
                } else {
                    ++it;
                }
                idx++;
            }
        }
    }

    // Spheres Section
    if (ImGui::CollapsingHeader("Spheres", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!scene) {
            ImGui::TextDisabled("No scene loaded");
        } else {
            if (ImGui::Button("Add Sphere")) {
                Sphere newSphere;
                newSphere.name = "New Sphere";
                newSphere.position = glm::vec3(0.0f, 0.0f, -5.0f);
                newSphere.radius = 1.0f;
                newSphere.color = glm::vec4(0.0f, 0.5f, 1.0f, 1.0f);
                scene->spheres.push_back(newSphere);
            }

            ImGui::Text("Spheres: %zu", scene->spheres.size());

            int idx = 0;
            for (auto it = scene->spheres.begin(); it != scene->spheres.end();) {
                ImGui::PushID(("sphere_" + std::to_string(idx)).c_str());

                bool isSelected = scene->HasSelection() &&
                                 scene->selectedObject->type == Scene::ObjectType::Sphere &&
                                 scene->selectedObject->index == static_cast<size_t>(idx);

                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.7f, 1.0f, 0.6f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.4f, 0.8f, 1.0f, 0.8f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.5f, 0.9f, 1.0f, 1.0f));
                }

                std::string label = it->name.empty() ? "Sphere #" + std::to_string(idx + 1) : it->name;
                if (isSelected) {
                    label += " (Selected)";
                }

                bool open = ImGui::TreeNode(label.c_str());

                if (isSelected) {
                    ImGui::PopStyleColor(3);
                }

                ImGui::SameLine();
                
                // Styled Select button
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(200.0f/255.0f, 120.0f/255.0f, 50.0f/255.0f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(160.0f/255.0f, 90.0f/255.0f, 35.0f/255.0f, 1.0f));
                if (ImGui::SmallButton("Select")) {
                    scene->SelectObject(Scene::ObjectType::Sphere, idx);
                }
                ImGui::PopStyleColor(3);
                
                ImGui::SameLine();
                
                // Styled Remove button
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
                bool remove = ImGui::SmallButton("Remove");
                ImGui::PopStyleColor(3);

                if (open) {
                    Sphere& sphere = *it;
                    bool sphereChanged = false;

                    char nameBuffer[128];
                    std::strncpy(nameBuffer, sphere.name.c_str(), sizeof(nameBuffer));
                    nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
                        sphere.name = nameBuffer;
                        sphereChanged = true;
                    }

                    if (ImGui::DragFloat3("Position", &sphere.position[0], 0.1f)) {
                        sphereChanged = true;
                    }

                    if (ImGui::DragFloat3("Velocity", &sphere.velocity[0], 0.1f)) {
                        sphereChanged = true;
                    }

                    if (ImGui::DragFloat("Mass (kg)", &sphere.massKg, 100.0f, 0.1f, 1e10f)) {
                        sphereChanged = true;
                    }

                    if (ImGui::DragFloat("Radius", &sphere.radius, 0.1f, 0.1f, 100.0f)) {
                        sphereChanged = true;
                    }

                    if (ImGui::ColorEdit4("Color", &sphere.color[0])) {
                        sphereChanged = true;
                    }

                    if (sphereChanged && !scene->currentPath.empty()) {
                        scene->Serialize(scene->currentPath);
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();

                if (remove) {
                    if (isSelected) {
                        scene->ClearSelection();
                    }
                    it = scene->spheres.erase(it);
                    if (!scene->currentPath.empty()) {
                        scene->Serialize(scene->currentPath);
                    }
                } else {
                    ++it;
                }
                idx++;
            }
        }
    }

    ImGui::End();
}

} // namespace SimulationWindow

