#include "ParameterWidgets.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include "../Simulation/Scene.h"
#include "imgui.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <unordered_map>
#include <functional>
#include <nfd.h>

#include "imgui_internal.h"

struct ImGuiWindow;

namespace ParameterWidgets {

const char* GetGroupDisplayName(ParameterGroup group) {
    switch (group) {
        case ParameterGroup::Window: return "Window";
        case ParameterGroup::Camera: return "Camera";
        case ParameterGroup::Rendering: return "Rendering";
        case ParameterGroup::Physics: return "Physics";
        case ParameterGroup::Debug: return "Debug";
        case ParameterGroup::Simulation: return "Simulation";
        case ParameterGroup::Application: return "Application";
        case ParameterGroup::Export: return "Export";
        case ParameterGroup::GeneralRelativity: return "General Relativity";
        default: return "Unknown";
    }
}

static void PushMultiItemsWidthsAndLabels(const char* labels[], int components, float w_full)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiStyle& style = GImGui->Style;
    if(w_full <= 0.0f)
        w_full = ImGui::GetWindowWidth();

    const float w_item_one =
        ImMax(1.0f, (w_full - (style.ItemInnerSpacing.x * 2.0f) * (components - 1)) / static_cast<float>(components)) -
        style.ItemInnerSpacing.x;
    for(int i = 0; i < components; i++)
        window->DC.ItemWidthStack.push_back(w_item_one - ImGui::CalcTextSize(labels[i]).x);
    window->DC.ItemWidth = window->DC.ItemWidthStack.back();
}

bool DragFloatNEx(const char* labels[], float* v, int components, float v_speed) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if(window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    bool value_changed = false;
    PushMultiItemsWidthsAndLabels(labels, components, 0.0f);
    if (ImGui::BeginTable("Vec3", 3, ImGuiTableFlags_None))
    {
        ImGui::TableNextRow();
        for(int i = 0; i < components; i++)
        {
            ImGui::TableNextColumn();
            ImGui::NewLine();
            ImGui::PushID(labels[i]);
            ImGui::PushID(i);
            const float availWidth = ImGui::GetContentRegionAvail().x;
            const float textWidth = ImGui::CalcTextSize(labels[i], ImGui::FindRenderedTextEnd(labels[i])).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - textWidth) * 0.5f);
            ImGui::TextUnformatted(labels[i], ImGui::FindRenderedTextEnd(labels[i]));
            ImGui::SetNextItemWidth(-1);
            value_changed |= ImGui::DragFloat("", &v[i], v_speed);
            ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    return value_changed;
}

void RenderParameter(const ParameterHandle& handle, UI* ui, WidgetStyle style) {
    auto& registry = ParameterRegistry::Instance();
    auto metaOpt = registry.GetMetadata(handle);

    if (!metaOpt.has_value()) {
        return;
    }

    const auto& meta = metaOpt.value();

    if (!meta.showInUI) {
        return;
    }

    const float labelWidth = (style == WidgetStyle::Compact) ? 120.0f : 150.0f;
    const bool showTooltip = (style != WidgetStyle::Compact);
    const bool showUnits = (style == WidgetStyle::Detailed);

    ImGui::PushID(static_cast<int>(meta.id));

    bool valueChanged = false;

    switch (meta.type) {
        case ParameterType::Bool: {
            bool value = registry.Get(handle, std::get<bool>(meta.defaultValue));

            if (ImGui::Checkbox(meta.displayName.c_str(), &value)) {
                registry.Set(handle, value);
                ui->MarkConfigDirty();
                valueChanged = true;
            }

            if (showTooltip && !meta.tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", meta.tooltip.c_str());
            }

            if (style == WidgetStyle::Detailed && !meta.isReadOnly) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", value ? "Enabled" : "Disabled");
            }
            break;
        }

        case ParameterType::Int: {
            int value = registry.Get(handle, std::get<int>(meta.defaultValue));

            if (!meta.enumValues.empty()) {
                if (ImGui::BeginCombo(meta.displayName.c_str(),
                                     value >= 0 && value < static_cast<int>(meta.enumValues.size())
                                     ? meta.enumValues[value].c_str() : "Unknown")) {
                    for (size_t i = 0; i < meta.enumValues.size(); ++i) {
                        bool isSelected = (value == static_cast<int>(i));
                        if (ImGui::Selectable(meta.enumValues[i].c_str(), isSelected)) {
                            registry.Set(handle, static_cast<int>(i));
                            ui->MarkConfigDirty();
                            valueChanged = true;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            } else if (meta.minValue != 0.0f || meta.maxValue != 0.0f) {
                const float availWidth = ImGui::GetContentRegionAvail().x;
                const float textWidth = ImGui::CalcTextSize(meta.displayName.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - textWidth) * 0.5f);
                ImGui::TextUnformatted(meta.displayName.c_str());

                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderInt(("##slider_" + meta.name).c_str(), &value,
                                    static_cast<int>(meta.minValue),
                                    static_cast<int>(meta.maxValue))) {
                    registry.Set(handle, value);
                    ui->MarkConfigDirty();
                    valueChanged = true;
                }
            } else {
                const float availWidth = ImGui::GetContentRegionAvail().x;
                const float textWidth = ImGui::CalcTextSize(meta.displayName.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - textWidth) * 0.5f);
                ImGui::TextUnformatted(meta.displayName.c_str());

                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragInt(("##drag_" + meta.name).c_str(), &value)) {
                    registry.Set(handle, value);
                    ui->MarkConfigDirty();
                    valueChanged = true;
                }
            }

            if (showTooltip && !meta.tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", meta.tooltip.c_str());
            }
            break;
        }

        case ParameterType::Float: {
            float value = registry.Get(handle, std::get<float>(meta.defaultValue));

            if (!meta.scaleValueNames.empty() && !meta.scaleValues.empty()) {
                if (ImGui::BeginCombo(meta.displayName.c_str(),
                                     std::to_string(value).c_str())) {
                    for (size_t i = 0; i < meta.scaleValues.size(); ++i) {
                        bool isSelected = (std::abs(value - meta.scaleValues[i]) < 0.001f);
                        std::string label = meta.scaleValueNames[i] + " (" +
                                          std::to_string(meta.scaleValues[i]) + ")";
                        if (ImGui::Selectable(label.c_str(), isSelected)) {
                            registry.Set(handle, meta.scaleValues[i]);
                            ui->MarkConfigDirty();
                            valueChanged = true;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            } else if (meta.minValue != 0.0f || meta.maxValue != 0.0f) {
                if (style == WidgetStyle::Compact) {
                    const float availWidth = ImGui::GetContentRegionAvail().x;
                    const float textWidth = ImGui::CalcTextSize(meta.displayName.c_str()).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - textWidth) * 0.5f);
                    ImGui::TextUnformatted(meta.displayName.c_str());

                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderFloat(("##slider_" + meta.name).c_str(), &value,
                                          meta.minValue, meta.maxValue, "%.3f")) {
                        registry.Set(handle, value);
                        ui->MarkConfigDirty();
                        valueChanged = true;
                    }
                } else {
                    const float availWidth = ImGui::GetContentRegionAvail().x;
                    const float textWidth = ImGui::CalcTextSize(meta.displayName.c_str()).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - textWidth) * 0.5f);
                    ImGui::TextUnformatted(meta.displayName.c_str());

                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::DragFloat(("##drag" + meta.name).c_str(), &value,
                                        meta.dragSpeed > 0 ? meta.dragSpeed : 0.1f,
                                        meta.minValue, meta.maxValue, "%.3f")) {
                        registry.Set(handle, value);
                        ui->MarkConfigDirty();
                        valueChanged = true;
                    }
                }
            } else {
                const float availWidth = ImGui::GetContentRegionAvail().x;
                const float textWidth = ImGui::CalcTextSize(meta.displayName.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - textWidth) * 0.5f);
                ImGui::TextUnformatted(meta.displayName.c_str());

                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat(("##drag" + meta.name).c_str(), &value,
                                    meta.dragSpeed > 0 ? meta.dragSpeed : 0.1f)) {
                    registry.Set(handle, value);
                    ui->MarkConfigDirty();
                    valueChanged = true;
                }
            }

            if (showTooltip && !meta.tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", meta.tooltip.c_str());
            }

            if (showUnits && (meta.minValue != 0.0f || meta.maxValue != 0.0f)) {
                ImGui::SameLine();
                ImGui::TextDisabled("[%.2f - %.2f]", meta.minValue, meta.maxValue);
            }
            break;
        }

        case ParameterType::String: {
            std::string value = registry.Get(handle, std::get<std::string>(meta.defaultValue));
            char buffer[256];
            strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';

            if (ImGui::InputText(meta.displayName.c_str(), buffer, sizeof(buffer))) {
                registry.Set(handle, std::string(buffer));
                ui->MarkConfigDirty();
                valueChanged = true;
            }

            if (showTooltip && !meta.tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", meta.tooltip.c_str());
            }
            break;
        }

        case ParameterType::Vec3: {
            glm::vec3 value = registry.Get(handle, std::get<glm::vec3>(meta.defaultValue));

            const char* names[] = {"X-Value", "Y-Value", "Z-Value"};
            if (DragFloatNEx(names, &value[0], 3, 0.05f)){
                registry.Set(handle, value);
                ui->MarkConfigDirty();
                valueChanged = true;
            }

            if (showTooltip && !meta.tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", meta.tooltip.c_str());
            }
            break;
        }

        case ParameterType::StringVector: {
            auto value = registry.Get(handle, std::get<std::vector<std::string>>(meta.defaultValue));

            if (ImGui::TreeNode(meta.displayName.c_str())) {
                for (size_t i = 0; i < value.size(); ++i) {
                    ImGui::BulletText("%s", value[i].c_str());
                }
                ImGui::TreePop();
            }

            if (showTooltip && !meta.tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", meta.tooltip.c_str());
            }
            break;
        }

        default:
            ImGui::TextDisabled("%s: (unsupported type)", meta.displayName.c_str());
            break;
    }

    if (meta.isReadOnly && style == WidgetStyle::Detailed) {
        ImGui::SameLine();
        ImGui::TextDisabled("[Read-Only]");
    }

    ImGui::PopID();
}

void RenderParameterGroup(ParameterGroup group, UI* ui, WidgetStyle style, bool defaultOpen) {
    RenderParameterGroupWithFilter(group, ui, [](const ParameterMetadata&) { return true; }, style, defaultOpen);
}

void RenderParameterGroupWithFilter(ParameterGroup group, UI* ui,
                                    std::function<bool(const ParameterMetadata&)> filter,
                                    WidgetStyle style, bool defaultOpen) {
    auto& registry = ParameterRegistry::Instance();
    const auto& allMeta = registry.GetAllMetadata();

    std::vector<ParameterMetadata> groupParams;
    for (const auto& [id, meta] : allMeta) {
        if (meta.group == group && meta.showInUI && filter(meta)) {
            groupParams.push_back(meta);
        }
    }

    if (groupParams.empty()) {
        return;
    }

    std::sort(groupParams.begin(), groupParams.end(),
              [](const ParameterMetadata& a, const ParameterMetadata& b) {
                  return a.name < b.name;
              });

    ImGuiTreeNodeFlags flags = defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0;

    if (ImGui::CollapsingHeader(GetGroupDisplayName(group), flags)) {
        if (style == WidgetStyle::Detailed) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextWrapped("Configure %s settings", GetGroupDisplayName(group));
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Spacing();
        }

        for (const auto& meta : groupParams) {
            RenderParameter(ParameterHandle(meta.id), ui, style);

            if (style != WidgetStyle::Compact) {
                ImGui::Spacing();
            }
        }

        if (style == WidgetStyle::Detailed) {
            ImGui::Spacing();
            ImGui::Separator();
        }
    }
}

bool RenderObjectParameter(SceneObject* object, const ParameterHandle& handle, const ParameterMetadata& meta,
                           WidgetStyle style) {
    if (!meta.showInUI) {
        return false;
    }

    ImGui::PushID(static_cast<int>(meta.id));

    bool valueChanged = false;
    ParameterValue currentValue = object->GetParameter(handle);

    switch (meta.type) {
        case ParameterType::Bool: {
            bool value = std::holds_alternative<bool>(currentValue) ? std::get<bool>(currentValue) :
                        std::get<bool>(meta.defaultValue);
            if (ImGui::Checkbox(meta.displayName.c_str(), &value)) {
                object->SetParameter(handle, value);
                valueChanged = true;
            }
            if (!meta.tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", meta.tooltip.c_str());
            }
            break;
        }

        case ParameterType::Int: {
            int value = std::holds_alternative<int>(currentValue) ? std::get<int>(currentValue) :
                       std::get<int>(meta.defaultValue);

            if (!meta.enumValues.empty()) {
                if (ImGui::BeginCombo(meta.displayName.c_str(),
                                     value >= 0 && value < static_cast<int>(meta.enumValues.size())
                                     ? meta.enumValues[value].c_str() : "Unknown")) {
                    for (size_t i = 0; i < meta.enumValues.size(); ++i) {
                        bool isSelected = (value == static_cast<int>(i));
                        if (ImGui::Selectable(meta.enumValues[i].c_str(), isSelected)) {
                            object->SetParameter(handle, static_cast<int>(i));
                            valueChanged = true;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            } else if (meta.minValue != 0.0f || meta.maxValue != 0.0f) {
                if (ImGui::SliderInt(meta.displayName.c_str(), &value,
                                    static_cast<int>(meta.minValue),
                                    static_cast<int>(meta.maxValue))) {
                    object->SetParameter(handle, value);
                    valueChanged = true;
                }
            } else {
                if (ImGui::DragInt(meta.displayName.c_str(), &value)) {
                    object->SetParameter(handle, value);
                    valueChanged = true;
                }
            }

            if (!meta.tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", meta.tooltip.c_str());
            }
            break;
        }

        case ParameterType::Float: {
            float value = std::holds_alternative<float>(currentValue) ? std::get<float>(currentValue) :
                         std::get<float>(meta.defaultValue);

            if (!meta.scaleValueNames.empty() && !meta.scaleValues.empty()) {
                // Track scale index per object+parameter
                static std::unordered_map<std::pair<void*, uint64_t>, size_t,
                    decltype([](const auto& p) {
                        return std::hash<void*>{}(p.first) ^ std::hash<uint64_t>{}(p.second);
                    })> scaleIndices;

                auto key = std::make_pair(static_cast<void*>(object), handle.m_Id);
                if (scaleIndices.find(key) == scaleIndices.end()) {
                    scaleIndices[key] = 0; // Default to first unit
                }
                size_t& currentScaleIdx = scaleIndices[key];

                // Convert value to display units
                float displayValue = value / meta.scaleValues[currentScaleIdx];
                float dragSpeedScaled = (meta.dragSpeed > 0 ? meta.dragSpeed : 0.1f) / meta.scaleValues[currentScaleIdx];

                // Label
                ImGui::Text("%s", meta.displayName.c_str());

                // Value field (full width)
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat(("##value_" + meta.name).c_str(), &displayValue, dragSpeedScaled,
                                    0.0f, 0.0f, "%.6g")) {
                    float newValue = displayValue * meta.scaleValues[currentScaleIdx];
                    object->SetParameter(handle, newValue);
                    valueChanged = true;
                }

                // Unit selector on next line (full width)
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo(("##scale_" + meta.name).c_str(),
                                     meta.scaleValueNames[currentScaleIdx].c_str())) {
                    for (size_t i = 0; i < meta.scaleValues.size(); ++i) {
                        bool isSelected = (i == currentScaleIdx);
                        if (ImGui::Selectable(meta.scaleValueNames[i].c_str(), isSelected)) {
                            currentScaleIdx = i;
                            valueChanged = true;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            } else if (meta.minValue != 0.0f || meta.maxValue != 0.0f) {
                if (ImGui::DragFloat(meta.displayName.c_str(), &value,
                                    meta.dragSpeed > 0 ? meta.dragSpeed : 0.1f,
                                    meta.minValue, meta.maxValue, "%.3f")) {
                    object->SetParameter(handle, value);
                    valueChanged = true;
                }
            } else {
                if (ImGui::DragFloat(meta.displayName.c_str(), &value,
                                    meta.dragSpeed > 0 ? meta.dragSpeed : 0.1f)) {
                    object->SetParameter(handle, value);
                    valueChanged = true;
                }
            }

            if (!meta.tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", meta.tooltip.c_str());
            }
            break;
        }

        case ParameterType::String: {
            std::string value = std::holds_alternative<std::string>(currentValue) ?
                               std::get<std::string>(currentValue) :
                               std::get<std::string>(meta.defaultValue);

            if (meta.name.find("FilePath") != std::string::npos || meta.name.find("Path") != std::string::npos) {
                char buffer[512];
                strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = '\0';

                ImGui::PushItemWidth(-50.0f);
                if (ImGui::InputText(meta.displayName.c_str(), buffer, sizeof(buffer))) {
                    object->SetParameter(handle, std::string(buffer));
                    valueChanged = true;
                }
                ImGui::PopItemWidth();

                ImGui::SameLine();
                if (ImGui::Button("...##FilePicker")) {
                    nfdchar_t* outPath = nullptr;
                    nfdfilteritem_t filterItems[] = {
                        { "GLTF/GLB", "gltf,glb" },
                        { "Images", "png,jpg,jpeg,hdr" },
                        { "All Files", "*" }
                    };
                    nfdresult_t result = NFD_OpenDialog(&outPath, filterItems, 3, nullptr);

                    if (result == NFD_OKAY && outPath) {
                        object->SetParameter(handle, std::string(outPath));
                        free(outPath);
                        valueChanged = true;
                    }
                }
            } else {
                char buffer[256];
                strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
                buffer[sizeof(buffer) - 1] = '\0';

                if (ImGui::InputText(meta.displayName.c_str(), buffer, sizeof(buffer))) {
                    object->SetParameter(handle, std::string(buffer));
                    valueChanged = true;
                }
            }

            if (!meta.tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", meta.tooltip.c_str());
            }
            break;
        }

        case ParameterType::Vec3: {
            glm::vec3 value = std::holds_alternative<glm::vec3>(currentValue) ?
                             std::get<glm::vec3>(currentValue) :
                             std::get<glm::vec3>(meta.defaultValue);

            if (!meta.scaleValueNames.empty() && !meta.scaleValues.empty()) {
                // Track scale index per object+parameter
                static std::unordered_map<std::pair<void*, uint64_t>, size_t,
                    decltype([](const auto& p) {
                        return std::hash<void*>{}(p.first) ^ std::hash<uint64_t>{}(p.second);
                    })> scaleIndices;

                auto key = std::make_pair(static_cast<void*>(object), handle.m_Id);
                if (scaleIndices.find(key) == scaleIndices.end()) {
                    scaleIndices[key] = 0; // Default to first unit
                }
                size_t& currentScaleIdx = scaleIndices[key];

                // Convert value to display units
                glm::vec3 displayValue = value / meta.scaleValues[currentScaleIdx];
                float dragSpeedScaled = (meta.dragSpeed > 0 ? meta.dragSpeed : 0.1f) / meta.scaleValues[currentScaleIdx];

                // Label
                ImGui::Text("%s", meta.displayName.c_str());

                // Value field (full width)
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat3(("##value_" + meta.name).c_str(), &displayValue[0], dragSpeedScaled,
                                     0.0f, 0.0f, "%.6g")) {
                    glm::vec3 newValue = displayValue * meta.scaleValues[currentScaleIdx];
                    object->SetParameter(handle, newValue);
                    valueChanged = true;
                }

                // Unit selector on next line (full width)
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo(("##scale_" + meta.name).c_str(),
                                     meta.scaleValueNames[currentScaleIdx].c_str())) {
                    for (size_t i = 0; i < meta.scaleValues.size(); ++i) {
                        bool isSelected = (i == currentScaleIdx);
                        if (ImGui::Selectable(meta.scaleValueNames[i].c_str(), isSelected)) {
                            currentScaleIdx = i;
                            valueChanged = true;
                        }
                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                if (ImGui::DragFloat3(meta.displayName.c_str(), &value[0],
                                     meta.dragSpeed > 0 ? meta.dragSpeed : 0.1f)) {
                    object->SetParameter(handle, value);
                    valueChanged = true;
                }
            }

            if (!meta.tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", meta.tooltip.c_str());
            }
            break;
        }

        case ParameterType::Quat: {
            glm::quat value = std::holds_alternative<glm::quat>(currentValue) ?
                             std::get<glm::quat>(currentValue) :
                             std::get<glm::quat>(meta.defaultValue);

            float eulerAngles[3];
            eulerAngles[0] = glm::degrees(glm::pitch(value));
            eulerAngles[1] = glm::degrees(glm::yaw(value));
            eulerAngles[2] = glm::degrees(glm::roll(value));

            if (ImGui::DragFloat3((meta.displayName + " (deg)").c_str(), eulerAngles, 1.0f)) {
                glm::quat newQuat = glm::quat(glm::vec3(
                    glm::radians(eulerAngles[0]),
                    glm::radians(eulerAngles[1]),
                    glm::radians(eulerAngles[2])
                ));
                object->SetParameter(handle, newQuat);
                valueChanged = true;
            }

            if (!meta.tooltip.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", meta.tooltip.c_str());
            }
            break;
        }

        case ParameterType::StringVector: {
            auto value = std::holds_alternative<std::vector<std::string>>(currentValue) ?
                        std::get<std::vector<std::string>>(currentValue) :
                        std::get<std::vector<std::string>>(meta.defaultValue);

            if (ImGui::TreeNode(meta.displayName.c_str())) {
                for (size_t i = 0; i < value.size(); ++i) {
                    ImGui::BulletText("%s", value[i].c_str());
                }
                ImGui::TreePop();
            }
            break;
        }

        default:
            ImGui::TextDisabled("%s: (unsupported type)", meta.displayName.c_str());
            break;
    }

    if (meta.isReadOnly && style == WidgetStyle::Detailed) {
        ImGui::SameLine();
        ImGui::TextDisabled("[Read-Only]");
    }

    ImGui::PopID();
    return valueChanged;
}

void RenderSceneObjectParameters(SceneObject* object, WidgetStyle style) {
    if (!object) {
        return;
    }

    const auto& metadata = object->GetAllMetadata();

    std::map<std::string, std::vector<std::pair<uint64_t, ParameterMetadata>>> groupedParams;

    for (const auto& [id, meta] : metadata) {
        if (!meta.showInUI) {
            continue;
        }

        std::string group;
        size_t dotPos = meta.name.find('.');
        if (dotPos != std::string::npos) {
            group = meta.name.substr(0, dotPos);
        } else {
            group = "General";
        }

        groupedParams[group].push_back({id, meta});
    }
    bool firstGroup = true;
    for (auto& [groupName, params] : groupedParams) {
        std::sort(params.begin(), params.end(),
                 [](const auto& a, const auto& b) {
                     return a.second.name < b.second.name;
                 });

        // Extra spacing between sections
        if (!firstGroup) {
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Spacing();
        }

        // Orange uppercase section header
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
        ImGui::TextUnformatted(groupName.c_str());
        ImGui::PopStyleColor();

        // Orange divider line under the section header
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 0.6f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Spacing();

        for (const auto& [id, meta] : params) {
            RenderObjectParameter(object, ParameterHandle(id), meta, style);

            // More spacing between individual settings for readability
            if (style != WidgetStyle::Compact) {
                ImGui::Spacing();
                ImGui::Spacing();
            }
        }

        firstGroup = false;
    }
}

}

