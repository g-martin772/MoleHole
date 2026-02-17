//
// Created by leo on 11/28/25.
//

#include "SimulationWindow.h"
#include "UI.h"
#include "../Application/Application.h"
#include "../Simulation/Scene.h"
#include "IconsFontAwesome6.h"
#include "ParameterWidgets.h"
#include "imgui.h"
#include <nfd.h>

namespace SimulationWindow {

void RenderSimulationControls(UI* ui) {
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

void RenderObjectTypeSection(Scene* scene, const std::string& objectTypeName,
                             const std::function<void()>& createNewObject) {
    if (!scene) {
        return;
    }

    auto& simulation = Application::GetSimulation();
    const auto& objectDefinitions = simulation.GetObjectDefinitions();

    const SceneObjectDefinition* definition = nullptr;
    for (const auto& def : objectDefinitions) {
        if (def.name == objectTypeName) {
            definition = &def;
            break;
        }
    }

    if (!definition) {
        ImGui::TextDisabled("Definition for %s not found", objectTypeName.c_str());
        return;
    }

    if (ImGui::Button(("Add " + objectTypeName).c_str())) {
        createNewObject();
    }

    size_t objectCount = 0;
    for (const auto& obj : scene->objects) {
        if (obj.HasClass(objectTypeName)) {
            objectCount++;
        }
    }

    ImGui::Text("%s Objects: %zu", objectTypeName.c_str(), objectCount);

    int objIdx = 0;
    for (size_t i = 0; i < scene->objects.size(); ) {
        auto& obj = scene->objects[i];
        if (!obj.HasClass(objectTypeName)) {
            ++i;
            continue;
        }

        ImGui::PushID(("obj_" + objectTypeName + "_" + std::to_string(i)).c_str());

        ObjectType objType = ObjectType::DynamicObject;
        if (objectTypeName == "BlackHole") objType = ObjectType::BlackHole;
        else if (objectTypeName == "Mesh") objType = ObjectType::Mesh;
        else if (objectTypeName == "Sphere") objType = ObjectType::Sphere;

        bool isSelected = scene->HasSelection() &&
                         scene->selectedObject->type == objType &&
                         scene->selectedObject->index == i;

        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.7f, 1.0f, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.4f, 0.8f, 1.0f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.5f, 0.9f, 1.0f, 1.0f));
        }

        auto nameParam = obj.GetParameter(ParameterHandle("Entity.Name"));
        std::string objectName = std::holds_alternative<std::string>(nameParam) ?
                                std::get<std::string>(nameParam) :
                                (objectTypeName + " #" + std::to_string(objIdx + 1));

        std::string label = objectName;
        if (isSelected) {
            label += " (Selected)";
        }

        ImGui::PushID(static_cast<int>(i));
        bool open = ImGui::TreeNode("##ObjectNode", "%s", label.c_str());
        ImGui::PopID();

        if (isSelected) {
            ImGui::PopStyleColor(3);
        }

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(200.0f/255.0f, 120.0f/255.0f, 50.0f/255.0f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(160.0f/255.0f, 90.0f/255.0f, 35.0f/255.0f, 1.0f));
        if (ImGui::SmallButton("Select")) {
            scene->SelectObject(objType, i);
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
        bool remove = ImGui::SmallButton("Remove");
        ImGui::PopStyleColor(3);

        if (open) {
            ParameterWidgets::RenderSceneObjectParameters(&obj, ParameterWidgets::WidgetStyle::Standard);

            if (!scene->currentPath.empty()) {
                scene->Serialize(scene->currentPath);
            }

            ImGui::TreePop();
        }

        ImGui::PopID();

        if (remove) {
            if (isSelected) {
                scene->ClearSelection();
            }
            scene->objects.erase(scene->objects.begin() + i);
            if (!scene->currentPath.empty()) {
                scene->Serialize(scene->currentPath);
            }
        } else {
            ++i;
            ++objIdx;
        }
    }
}

void Render(UI* ui, Scene* scene) {
    if (!ImGui::Begin("Simulation")) {
        ImGui::End();
        return;
    }

    if (scene && scene->HasSelection()) {
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

    if (!scene) {
        ImGui::TextDisabled("No scene loaded");
        ImGui::End();
        return;
    }

    auto& simulation = Application::GetSimulation();
    const auto& objectDefinitions = simulation.GetObjectDefinitions();

    for (const auto& definition : objectDefinitions) {
        if (ImGui::CollapsingHeader(definition.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            RenderObjectTypeSection(scene, definition.name, [&]() {
                std::vector<std::string> classNames;
                for (const auto* objClass : definition.objectClasses) {
                    classNames.push_back(objClass->name);
                }

                SceneObject newObj(classNames);

                for (const auto* objClass : definition.objectClasses) {
                    for (const auto& reqParam : objClass->requiredParameterKeys) {
                        ParameterHandle handle(reqParam.c_str());
                        auto metaIt = objClass->meta.find(handle.m_Id);
                        if (metaIt != objClass->meta.end()) {
                            const auto& meta = metaIt->second;
                            newObj.SetParameter(handle, meta.defaultValue);
                        }
                    }
                }

                newObj.SetParameter(ParameterHandle("Entity.Name"), definition.name);
                newObj.SetParameter(ParameterHandle("Entity.Position"), glm::vec3(0.0f, 0.0f, -5.0f));

                scene->objects.push_back(std::move(newObj));
                if (!scene->currentPath.empty()) {
                    scene->Serialize(scene->currentPath);
                }
            });
        }
    }

    ImGui::End();
}

} // namespace SimulationWindow

