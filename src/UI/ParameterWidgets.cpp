#include "ParameterWidgets.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include "imgui.h"
#include <spdlog/spdlog.h>
#include <algorithm>

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
        default: return "Unknown";
    }
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
                if (ImGui::SliderInt(meta.displayName.c_str(), &value,
                                    static_cast<int>(meta.minValue),
                                    static_cast<int>(meta.maxValue))) {
                    registry.Set(handle, value);
                    ui->MarkConfigDirty();
                    valueChanged = true;
                }
            } else {
                if (ImGui::DragInt(meta.displayName.c_str(), &value)) {
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
                    if (ImGui::SliderFloat(meta.displayName.c_str(), &value,
                                          meta.minValue, meta.maxValue, "%.3f")) {
                        registry.Set(handle, value);
                        ui->MarkConfigDirty();
                        valueChanged = true;
                    }
                } else {
                    if (ImGui::DragFloat(meta.displayName.c_str(), &value,
                                        meta.dragSpeed > 0 ? meta.dragSpeed : 0.1f,
                                        meta.minValue, meta.maxValue, "%.3f")) {
                        registry.Set(handle, value);
                        ui->MarkConfigDirty();
                        valueChanged = true;
                    }
                }
            } else {
                if (ImGui::DragFloat(meta.displayName.c_str(), &value,
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

            if (ImGui::DragFloat3(meta.displayName.c_str(), &value[0],
                                 meta.dragSpeed > 0 ? meta.dragSpeed : 0.1f)) {
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

}

