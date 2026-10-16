#include <imgui.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    // Combo over f32/f64/f128
    bool PrecisionCombo(const char* id, Units::Precision* value,
                        const std::function<const char*(Units::Precision)>& unavailableReason)
    {
        bool changed = false;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
        if (ImGui::BeginCombo(id, std::string(Units::PrecisionName(*value)).c_str()))
        {
            for (const auto precision : Units::kPrecisions)
            {
                const char* reason = unavailableReason(precision);
                std::string label(Units::PrecisionName(precision));
                if (reason) label += " (unavailable)";
                if (ImGui::Selectable(label.c_str(), precision == *value, reason ? ImGuiSelectableFlags_Disabled : 0))
                {
                    *value = precision;
                    changed = true;
                }
                if (reason && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", reason);
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    struct DebugWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, UiState, RenderSettingsService>;

        DebugWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<UiState> uiState,
                         std::shared_ptr<RenderSettingsService> renderSettings)
            : HotReloadableLayer(logger), m_UiState(std::move(uiState)), m_RenderSettings(std::move(renderSettings))
        {
        }

        void OnUiRender() override
        {
            if (!m_UiState->ShowDebugWindow) return;
            if (!ImGui::Begin("Debug", &m_UiState->ShowDebugWindow))
            {
                ImGui::End();
                return;
            }

            SectionHeader("DISPLAY UNITS");
            {
                const auto current = Units::GetDisplayPreset();
                ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
                if (ImGui::BeginCombo("##displayunits", std::string(Units::DisplayPresetName(current)).c_str()))
                {
                    for (const auto preset : Units::kDisplayPresets)
                    {
                        if (ImGui::Selectable(std::string(Units::DisplayPresetName(preset)).c_str(), preset == current))
                        {
                            Units::SetDisplayPreset(preset);
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Which units the inspector prefers for lengths, masses and times.\n"
                                       "Auto picks by magnitude; Human = m/km/kg, Stellar = Rsun/AU/Msun, Galactic = pc/Msun/yr.\n"
                                       "Right-click a value to force one unit for that field.");
                }
            }

            SectionHeader("CPU PRECISION");
            Units::Precision cpuPrecision = m_UiState->SimulationPrecision;
            if (PrecisionCombo("##cpuprecision", &cpuPrecision, [](Units::Precision p) -> const char*
                {
                    return Units::CpuSupports(p) ? nullptr : "This compiler has no std::float128_t";
                }))
            {
                m_UiState->SimulationPrecision = cpuPrecision;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Arithmetic type of MoleHole's gravity evaluation. Positions are stored in\n"
                                   "double either way; with f32 they are cast before subtraction.\n"
                                   "PhysX integration stays float32 (local origin).\n"
                                   "GPU precision is a render setting (Render window, Transport).");
            }

            const auto context = m_RenderSettings->ActiveContext();
            ImGui::Spacing();
            ImGui::TextDisabled("Editing the %s profile '%s'.", std::string(RenderContextLabel(context)).c_str(),
                                m_RenderSettings->ProfileFor(context).Name.c_str());
            DrawGroup("OVERLAYS", "Overlays", context);
            DrawGroup("DEBUG VIEW", "Debug", context);

            ImGui::End();
        }

    private:
        void DrawGroup(const char* title, const char* group, const RenderContext context)
        {
            const auto& registry = RenderSettingRegistry();
            const auto& set = m_RenderSettings->Profiles();
            const auto& profile = set.ProfileFor(context);
            const RenderSettings baseline = profile.BuiltIn ? profile.Settings
                                            : set.Find(profile.Base) ? set.Find(profile.Base)->Settings : RenderSettings{};
            RenderSettings edited = profile.Settings;
            bool changed = false;
            SectionHeader(title);
            for (const auto* setting : registry.InGroup(group))
            {
                ImGui::PushID(setting->Key.c_str());
                changed |= DrawRenderSetting(m_UiState->IconFont, *setting, edited, baseline, m_RenderSettings->Capabilities());
                ImGui::PopID();
            }
            if (changed) m_RenderSettings->Edit(context, edited);
        }

        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<RenderSettingsService> m_RenderSettings;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(DebugWindowLayer)
