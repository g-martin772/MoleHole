export module MoleHole:UI.RenderSettingsWidgets;

import std;
import glm;
import imgui;
import GPP;
import :UI.WidgetLogic;
import :UI.Widgets;
import :Rendering.Settings;

export namespace MoleHole
{
    // Five pips, filled up to `cost` (0-5); free settings draw nothing
    inline void DrawCostBadge(const int cost)
    {
        if (cost <= 0) return;
        const float pipWidth = 4.0f;
        const float gap = 2.0f;
        const float height = ImGui::GetFrameHeight() * 0.5f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = 5.0f * pipWidth + 4.0f * gap;
        ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
        auto* list = ImGui::GetWindowDrawList();
        const glm::vec4 low = HexColor(0x5cb85c);
        const glm::vec4 high = HexColor(0xd9534f);
        for (int i = 0; i < 5; ++i)
        {
            const float x = pos.x + static_cast<float>(i) * (pipWidth + gap);
            const float y = pos.y + (ImGui::GetFrameHeight() - height) * 0.5f;
            const bool on = i < cost;
            const glm::vec4 color = on ? LerpColor(low, high, static_cast<float>(i) / 4.0f) : HexColor(0x3a3a3a);
            list->AddRectFilled(ImVec2(x, y), ImVec2(x + pipWidth, y + height), ToU32(color), 1.0f);
        }
    }

    [[nodiscard]] inline std::string RenderSettingTooltip(const GPP::SettingDesc<RenderSettings>& setting,
                                                          const RenderSettings& value, const GPP::GpuCapabilities& caps)
    {
        std::string text = setting.Meta.Tooltip;
        const int cost = setting.CostFor(value);
        if (!text.empty()) text += "\n\n";
        text += std::format("{}\nCost: {}/5", setting.Key, cost);
        const auto required = setting.RequiresFor(value);
        if (!caps.Has(required)) text += std::format("\nNeeds {}, which this GPU lacks.", GPP::CapabilityLabel(required));
        if (!setting.Meta.Doc.empty()) text += "\nSee docs/" + setting.Meta.Doc;
        return text;
    }

    // One row: label, widget, cost badge, reset to `baseline`. Returns true when `value` changed.
    inline bool DrawRenderSetting(ImFont* icons, const GPP::SettingDesc<RenderSettings>& setting, RenderSettings& value,
                                  const RenderSettings& baseline, const GPP::GpuCapabilities& caps)
    {
        using GPP::FieldType;
        const auto& meta = setting.Meta;
        const bool modified = setting.Get(value) != setting.Get(baseline);
        const bool wholeUnavailable = !caps.Has(meta.Requires);
        const auto tooltip = RenderSettingTooltip(setting, value, caps);
        bool changed = false;

        const auto draw = [&]
        {
            const int cost = setting.CostFor(value);
            const float badgeWidth = cost > 0 ? 5.0f * 4.0f + 4.0f * 2.0f + ImGui::GetStyle().ItemInnerSpacing.x : 0.0f;
            const float width = std::max(30.0f, ImGui::CalcItemWidth() - badgeWidth);
            ImGui::SetNextItemWidth(width);
            if (wholeUnavailable) ImGui::BeginDisabled();

            const auto flags = (meta.Scale == GPP::SettingScale::Log ? ImGuiSliderFlags_Logarithmic : 0)
                               | ImGuiSliderFlags_AlwaysClamp;
            const bool bounded = meta.Max > meta.Min;
            switch (setting.Type)
            {
            case FieldType::Bool:
            {
                bool flag = std::get<bool>(setting.Get(value));
                if (ImGui::Checkbox("##v", &flag)) changed = setting.Set(value, GPP::FieldValue{flag});
                break;
            }
            case FieldType::Int:
            {
                int number = std::get<int>(setting.Get(value));
                const bool edited = bounded
                                        ? ImGui::SliderInt("##v", &number, static_cast<int>(meta.Min),
                                                           static_cast<int>(meta.Max), "%d", flags)
                                        : ImGui::DragInt("##v", &number);
                if (edited) changed = setting.Set(value, GPP::FieldValue{number});
                break;
            }
            case FieldType::Float:
            {
                float number = std::get<float>(setting.Get(value));
                const char* format = meta.Format.empty() ? "%.3f" : meta.Format.c_str();
                const bool edited = bounded
                                        ? ImGui::SliderFloat("##v", &number, static_cast<float>(meta.Min),
                                                             static_cast<float>(meta.Max), format, flags)
                                        : ImGui::DragFloat("##v", &number, 0.01f, 0.0f, 0.0f, format);
                if (edited) changed = setting.Set(value, GPP::FieldValue{number});
                break;
            }
            case FieldType::Enum:
            {
                int index = std::get<int>(setting.Get(value));
                const bool valid = index >= 0 && static_cast<std::size_t>(index) < meta.Options.size();
                const char* preview = valid ? meta.OptionLabel(static_cast<std::size_t>(index)).c_str() : "";
                if (ImGui::BeginCombo("##v", preview))
                {
                    for (std::size_t i = 0; i < meta.Options.size(); ++i)
                    {
                        const std::string required = i < meta.OptionRequires.size() ? meta.OptionRequires[i] : std::string();
                        const bool available = caps.Has(required);
                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::Selectable(meta.OptionLabel(i).c_str(), static_cast<int>(i) == index,
                                              available ? 0 : ImGuiSelectableFlags_Disabled))
                        {
                            changed = setting.Set(value, GPP::FieldValue{static_cast<int>(i)});
                        }
                        if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        {
                            ImGui::SetTooltip("Needs %s, which this GPU lacks.", std::string(GPP::CapabilityLabel(required)).c_str());
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            default: ImGui::TextDisabled("(unsupported)"); break;
            }
            if (wholeUnavailable) ImGui::EndDisabled();
            if (cost > 0)
            {
                ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                DrawCostBadge(cost);
            }
        };

        if (PropertyRow(icons, meta.Label.c_str(), modified, draw, tooltip.c_str()))
        {
            changed = setting.Set(value, setting.Get(baseline)) || changed;
        }
        return changed;
    }
}
