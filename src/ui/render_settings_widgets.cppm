export module MoleHole:UI.RenderSettingsWidgets;

import std;
import glm;
import imgui;
import GPP;
import :UI.WidgetLogic;
import :UI.Widgets;
import :Rendering.Settings;
import :Rendering.DebugLegend;
import :Rendering.SettingsService;

export namespace MoleHole
{
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

    inline ImU32 LegendU32(const LegendColor& color)
    {
        return ToU32(glm::vec4(color.R, color.G, color.B, 1.0f));
    }

    inline void DrawLegendEntry(const LegendEntry& entry)
    {
        const float size = ImGui::GetTextLineHeight();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        auto* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), LegendU32(entry.Color), 2.0f);
        list->AddRect(pos, ImVec2(pos.x + size, pos.y + size), ToU32(HexColor(0x808080)), 2.0f);
        ImGui::Dummy(ImVec2(size, size));
        ImGui::SameLine();
        ImGui::TextWrapped("%.*s - %.*s", static_cast<int>(entry.Label.size()), entry.Label.data(),
                           static_cast<int>(entry.Meaning.size()), entry.Meaning.data());
    }

    inline void DrawStepCountLegend()
    {
        constexpr int kSegments = 48;
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = ImGui::GetTextLineHeight();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        auto* list = ImGui::GetWindowDrawList();
        for (int i = 0; i < kSegments; ++i)
        {
            const float t0 = static_cast<float>(i) / kSegments;
            const float t1 = static_cast<float>(i + 1) / kSegments;
            const ImU32 left = LegendU32(DebugHeat(t0));
            const ImU32 right = LegendU32(DebugHeat(t1));
            list->AddRectFilledMultiColor(ImVec2(pos.x + t0 * width, pos.y), ImVec2(pos.x + t1 * width, pos.y + height),
                                          left, right, right, left);
        }
        list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), ToU32(HexColor(0x808080)));
        ImGui::Dummy(ImVec2(width, height));

        const ImVec2 labels = ImGui::GetCursorScreenPos();
        const ImU32 text = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const std::string low(kStepCountLowLabel);
        const std::string high(kStepCountHighLabel);
        list->AddText(labels, text, low.c_str());
        list->AddText(ImVec2(labels.x + width - ImGui::CalcTextSize(high.c_str()).x, labels.y), text, high.c_str());
        ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
        TextDisabledWrapped(kStepCountNote);
    }

    inline void DrawDebugViewLegend(const DebugView view)
    {
        if (view == DebugView::None) return;
        ImGui::Spacing();
        ImGui::TextDisabled("LEGEND");
        if (view == DebugView::StepCount) DrawStepCountLegend();
        else for (const auto& entry : kTerminationLegend) DrawLegendEntry(entry);
        ImGui::Spacing();
    }

    inline void DrawOverlayLegends(const RenderSettings& value)
    {
        bool any = false;
        for (const auto& legend : kOverlayLegends)
        {
            const auto* setting = RenderSettingRegistry().Find(legend.SettingKey);
            if (!setting || !std::get<bool>(setting->Get(value))) continue;
            if (!any) ImGui::Spacing();
            if (!any) ImGui::TextDisabled("LEGEND");
            any = true;
            ImGui::TextDisabled("%s", setting->Meta.Label.c_str());
            for (const auto& entry : legend.Entries) DrawLegendEntry(entry);
        }
        if (any) ImGui::Spacing();
    }

    inline void DrawRenderStatsOverlay(const ImVec2 corner, const std::optional<GPP::Renderer::BufferTargetStats>& stats,
                                       const RenderSettingsService& settings, const float governorScale)
    {
        if (!stats) return;
        const auto context = settings.ActiveContext();
        const auto text = std::format("{}: {} | {:.1f} fps, GPU {:.1f} ms | tracer {:.0f}%", RenderContextLabel(context),
                                      settings.ProfileFor(context).Name, stats->FramesPerSecond, stats->GpuMs,
                                      settings.Effective().ResolutionScale * governorScale);
        const ImVec2 size = ImGui::CalcTextSize(text.c_str());
        const ImVec2 pos(corner.x + 8.0f, corner.y + 8.0f);
        auto* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(ImVec2(pos.x - 4.0f, pos.y - 2.0f), ImVec2(pos.x + size.x + 4.0f, pos.y + size.y + 2.0f),
                            ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.6f)), 3.0f);
        list->AddText(pos, ImGui::GetColorU32(ImVec4(0.92f, 0.92f, 0.92f, 1.0f)), text.c_str());
    }
}
