//
// Created by leo on 11/28/25.
//

#include "SimulationWindow.h"
#include "UI.h"
#include "../Application/Application.h"
#include "../Simulation/Scene.h"
#include "IconsFontAwesome6.h"
#include "ParameterWidgets.h"
#include <nfd.h>
#include <filesystem>
#include <cstring>
#include <unordered_map>

namespace SimulationWindow {

void RenderSimulationControls(UI* ui) {
    auto& simulation = Application::GetSimulation();
    auto& renderer = Application::GetRenderer();

    ImVec2 viewportPos = ImVec2(renderer.m_viewportX, renderer.m_viewportY);
    ImVec2 viewportSize = ImVec2(renderer.m_viewportWidth, renderer.m_viewportHeight);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking;

    ImGui::SetNextWindowPos(ImVec2(viewportPos.x + viewportSize.x * 0.5f, viewportPos.y + 16), ImGuiCond_Always, ImVec2(0.5f, 0.0f));

    if (ImGui::Begin("##SimulationControls", nullptr, flags)) {
        constexpr float buttonSize = 64.0f;

        bool isStopped = simulation.IsStopped();
        bool isPaused = simulation.IsPaused();

        if (ui->GetIconFont()) {
            ImGui::PushFont(ui->GetIconFont());
        }

        if (isStopped || isPaused) {
            // Play button is primary action - orange
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(200.0f/255.0f, 120.0f/255.0f, 50.0f/255.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(160.0f/255.0f, 90.0f/255.0f, 35.0f/255.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (ImGui::Button(ui->GetIconFont() ? ICON_FA_PLAY : (isPaused ? "|>" : ">>"), ImVec2(buttonSize, buttonSize))) {
                simulation.Start();
            }
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(isPaused ? "Resume" : "Start");
            }
        } else {
            // Pause button - orange when running
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(200.0f/255.0f, 120.0f/255.0f, 50.0f/255.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(160.0f/255.0f, 90.0f/255.0f, 35.0f/255.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            if (ImGui::Button(ui->GetIconFont() ? ICON_FA_PAUSE : "||", ImVec2(buttonSize, buttonSize))) {
                simulation.Pause();
            }
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Pause");
            }
        }

        ImGui::SameLine();

        // Stop button - orange when running/paused, zinc-800 when already stopped
        if (!isStopped) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(200.0f/255.0f, 120.0f/255.0f, 50.0f/255.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(160.0f/255.0f, 90.0f/255.0f, 35.0f/255.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.14f, 0.14f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        }
        if (ImGui::Button(ui->GetIconFont() ? ICON_FA_STOP : "[]", ImVec2(buttonSize, buttonSize))) {
            simulation.Stop();
        }
        ImGui::PopStyleColor(4);
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
    ImGui::PopStyleVar(3);
}

void RenderObjectTypeSection(UI* ui, Scene* scene, const std::string& objectTypeName,
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

    // Full-width orange "Add" button matching design
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(200.0f/255.0f, 120.0f/255.0f, 50.0f/255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(160.0f/255.0f, 90.0f/255.0f, 35.0f/255.0f, 1.0f));
    if (ImGui::Button(("Add " + objectTypeName).c_str(), ImVec2(-1, 0))) {
        createNewObject();
    }
    ImGui::PopStyleColor(3);

    ImGui::Spacing();

    // Track which objects are expanded in the accordion
    static std::unordered_map<std::string, bool> expandedObjects;

    int objIdx = 0;
    for (size_t i = 0; i < scene->objects.size(); ) {
        auto& obj = scene->objects[i];
        if (!obj.HasClass(objectTypeName)) {
            ++i;
            continue;
        }

        std::string objId = objectTypeName + "_" + std::to_string(i);
        ImGui::PushID(objId.c_str());

        ObjectType objType = ObjectType::DynamicObject;
        if (objectTypeName == "BlackHole") objType = ObjectType::BlackHole;
        else if (objectTypeName == "Mesh") objType = ObjectType::Mesh;
        else if (objectTypeName == "Sphere") objType = ObjectType::Sphere;

        bool isSelected = scene->HasSelection() &&
                         scene->selectedObject->type == objType &&
                         scene->selectedObject->index == i;

        auto nameParam = obj.GetParameter(ParameterHandle("Entity.Name"));
        std::string objectName = std::holds_alternative<std::string>(nameParam) ?
                                std::get<std::string>(nameParam) :
                                (objectTypeName + " #" + std::to_string(objIdx + 1));

        bool& isExpanded = expandedObjects[objId];

        // Separator line between items (zinc-700)
        if (objIdx > 0) {
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.32f, 0.32f, 0.32f, 1.0f));
            ImGui::Separator();
            ImGui::PopStyleColor();
        }

        // --- Object row: [Chevron + Name (left)]  [Select btn] [Delete btn] (right) ---
        float rowHeight = ImGui::GetFrameHeight();
        float availWidth = ImGui::GetContentRegionAvail().x;
        constexpr float iconBtnSize = 24.0f;
        constexpr float iconBtnSpacing = 4.0f;
        float buttonsWidth = iconBtnSize * 2 + iconBtnSpacing;

        // Full-width orange button for the name area
        float nameAreaWidth = availWidth - buttonsWidth - iconBtnSpacing;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(200.0f/255.0f, 120.0f/255.0f, 50.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(160.0f/255.0f, 90.0f/255.0f, 35.0f/255.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

        // Build label: chevron + name
        ImFont* iconFont = ui->GetIconFont();
        const char* chevron = isExpanded ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT;
        std::string rowLabel;
        if (iconFont) {
            // We'll draw the chevron manually over the button so it uses the icon font
            rowLabel = "         " + objectName; // reserve space for chevron
        } else {
            rowLabel = std::string(isExpanded ? "v " : "> ") + objectName;
        }

        ImVec2 btnPos = ImGui::GetCursorScreenPos();
        if (ImGui::Button(rowLabel.c_str(), ImVec2(nameAreaWidth, 0))) {
            isExpanded = !isExpanded;
        }

        // Draw the chevron icon on top of the button using the icon font
        if (iconFont) {
            float textY = btnPos.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
            float iconSize = ImGui::GetTextLineHeight();
            ImVec2 chevronPos = ImVec2(btnPos.x + ImGui::GetStyle().FramePadding.x, textY);
            ImGui::GetWindowDrawList()->AddText(iconFont, iconSize, chevronPos, IM_COL32(255, 255, 255, 255), chevron);
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        // Place icon buttons on the same line, right-aligned and vertically centered
        ImGui::SameLine(nameAreaWidth + 2.5 * iconBtnSpacing);

        // Compute padding to vertically center icon text inside a button matching rowHeight
        bool hasIconFont = ui->GetIconFont() != nullptr;
        if (hasIconFont) ImGui::PushFont(ui->GetIconFont());
        float iconTextHeight = ImGui::GetFontSize();
        if (hasIconFont) ImGui::PopFont();
        float vertPad = (rowHeight - iconTextHeight) * 0.5f;
        float horizPad = (iconBtnSize - iconTextHeight) * 0.5f;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(horizPad, vertPad));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(200.0f/255.0f, 120.0f/255.0f, 50.0f/255.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(160.0f/255.0f, 90.0f/255.0f, 35.0f/255.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 0.8f));
        }

        if (hasIconFont) ImGui::PushFont(ui->GetIconFont());

        // Select button with icon — height matches expansion button
        if (ImGui::Button(hasIconFont ? ICON_FA_CROSSHAIRS : "S", ImVec2(iconBtnSize, rowHeight))) {
            if (isSelected) {
                scene->ClearSelection();
            } else {
                scene->SelectObject(objType, i);
            }
        }
        ImGui::PopStyleColor(3);
        bool selectHovered = ImGui::IsItemHovered();

        ImGui::SameLine(0, iconBtnSpacing);

        // Delete button (trash icon) - red tint — height matches expansion button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.15f, 0.15f, 1.0f));

        bool removeClicked = ImGui::Button(hasIconFont ? ICON_FA_TRASH_CAN : "X", ImVec2(iconBtnSize, rowHeight));
        ImGui::PopStyleColor(3);
        bool deleteHovered = ImGui::IsItemHovered();

        if (hasIconFont) ImGui::PopFont();

        // Tooltips rendered with normal font
        if (selectHovered) {
            ImGui::SetTooltip(isSelected ? "Deselect" : "Select");
        }
        if (deleteHovered) {
            ImGui::SetTooltip("Remove");
        }

        ImGui::PopStyleVar(2);

        // Expanded accordion content
        if (isExpanded) {
            ImGui::Indent(8.0f);
            ImGui::Spacing();

            ParameterWidgets::RenderSceneObjectParameters(&obj, ParameterWidgets::WidgetStyle::Standard);

            if (!scene->currentPath.empty()) {
                scene->Serialize(scene->currentPath);
            }

            ImGui::Spacing();
            ImGui::Unindent(8.0f);
        }

        // Handle deletion after rendering
        if (removeClicked) {
            if (isSelected) {
                scene->ClearSelection();
            }
            expandedObjects.erase(objId);
            scene->objects.erase(scene->objects.begin() + static_cast<ptrdiff_t>(i));
            if (!scene->currentPath.empty()) {
                scene->Serialize(scene->currentPath);
            }
            ImGui::PopID();
            continue;
        }

        ImGui::PopID();
        ++i;
        ++objIdx;
    }
}

static std::function<void()> CreateObjectFactory(Scene* scene, const SceneObjectDefinition& definition) {
    return [scene, &definition]() {
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
    };
}

void Render(UI* ui, Scene* scene) {
    if (!ImGui::Begin("Simulation")) {
        ImGui::End();
        return;
    }

    if (!scene) {
        ImGui::TextDisabled("No scene loaded");
        ImGui::End();
        return;
    }

    if (scene->HasSelection()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 0.5f));
        ImGui::BeginChild("TransformControls", ImVec2(0, 80), true);

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
        if (ImGui::Checkbox("Snap", &useSnap)) {
            ui->SetUsingSnap(useSnap);
        }
        ImGui::SameLine();
        if (ImGui::Button("Deselect")) {
            scene->ClearSelection();
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    auto& simulation = Application::GetSimulation();
    const auto& objectDefinitions = simulation.GetObjectDefinitions();

    // Tabs styled to match design: zinc-800 background, orange-600 active tab
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));                    // bg-zinc-800
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(200.0f/255.0f, 120.0f/255.0f, 50.0f/255.0f, 1.0f)); // hover: orange-700
    ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));  // active: orange-600
    ImGui::PushStyleColor(ImGuiCol_TabUnfocused, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));

    if (ImGui::BeginTabBar("ObjectTabs")) {
        for (const auto& definition : objectDefinitions) {
            if (ImGui::BeginTabItem(definition.name.c_str())) {
                ImGui::Spacing();
                RenderObjectTypeSection(ui, scene, definition.name, CreateObjectFactory(scene, definition));
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::PopStyleColor(5);

    ImGui::End();
}

} // namespace SimulationWindow

