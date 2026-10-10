#include <imgui.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    void DebugModeTooltip(int debugMode)
    {
        const char* tooltip = "Unknown debug mode";
        switch (debugMode)
        {
        case 0: tooltip = "Normal rendering with no debug visualization"; break;
        case 1: tooltip = "Gravity Grid overlay on the ground plane"; break;
        case 2: tooltip = "Object trajectory paths (not yet implemented)"; break;
        default: break;
        }
        ImGui::SetTooltip("%s", tooltip);
    }

    struct DebugWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, UiState>;

        DebugWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_UiState(std::move(uiState))
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

            auto& render = m_UiState->Render;

            SectionHeader("RENDERING");
            ImGui::Checkbox("Black Holes", &render.RenderBlackHoles);
            ImGui::Checkbox("Spheres", &render.RenderSpheres);
            ImGui::Checkbox("Gravitational Lensing", &render.GravitationalLensing);
            ImGui::Checkbox("Gravitational Redshift", &render.GravitationalRedshift);
            ImGui::Checkbox("Doppler Beaming", &render.DopplerBeaming);
            ImGui::Checkbox("Physically Accurate", &render.PhysicallyAccurate);

            SectionHeader("ACCRETION DISK");
            ImGui::Checkbox("Enabled", &render.AccretionDisk);
            ImGui::Checkbox("Volumetric", &render.AccretionDiskVolumetric);
            ImGui::SliderFloat("Height", &render.AccDiskHeight, 0.0f, 2.0f);
            ImGui::SliderFloat("Speed", &render.AccDiskSpeed, 0.0f, 5.0f);
            ImGui::SliderFloat("Noise Scale", &render.AccDiskNoiseScale, 0.0f, 5.0f);
            ImGui::SliderFloat("Noise LOD", &render.AccDiskNoiseLOD, 0.0f, 10.0f);

            SectionHeader("RAY MARCHING");
            ImGui::SliderFloat("Step Size", &render.RayStepSize, 0.001f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderInt("Max Steps", &render.MaxRaySteps, 100, 200000);
            ImGui::SliderFloat("Adaptive Step Rate", &render.AdaptiveStepRate, 0.01f, 10.0f, "%.2f",
                               ImGuiSliderFlags_Logarithmic);

            SectionHeader("POST PROCESSING");
            ImGui::Checkbox("Anti-Aliasing (FXAA)", &render.AntiAliasing);
            ImGui::Checkbox("Lens Flare", &render.LensFlare);
            ImGui::SliderFloat("Flare Intensity", &render.LensFlareIntensity, 0.0f, 100.0f);
            ImGui::SliderFloat("Flare Threshold", &render.LensFlareThreshold, 0.0f, 5.0f);

            SectionHeader("THIRD PERSON");
            ImGui::Checkbox("Third Person Crosshair", &render.ThirdPerson);
            ImGui::SliderFloat("Distance", &render.ThirdPersonDistance, 1.0f, 100.0f);
            ImGui::SliderFloat("Height", &render.ThirdPersonHeight, 0.0f, 50.0f);

            SectionHeader("BLOOM");
            ImGui::Checkbox("Bloom", &render.BloomEnabled);
            ImGui::SliderFloat("Threshold", &render.BloomThreshold, 0.0f, 5.0f);
            ImGui::SliderInt("Blur Passes", &render.BloomBlurPasses, 1, 20);
            ImGui::SliderFloat("Intensity", &render.BloomIntensity, 0.0f, 100.0f);
            ImGui::Checkbox("Bloom Debug", &render.BloomDebug);

            SectionHeader("DEBUG VISUALIZATION");
            const char* debugModeItems[] = {"Normal Rendering", "Gravity Grid", "Object Paths"};
            ImGui::Combo("Debug Mode", &render.DebugMode, debugModeItems, IM_ARRAYSIZE(debugModeItems));
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) DebugModeTooltip(render.DebugMode);

            ImGui::Checkbox("Gravity Grid Overlay", &render.ShowGravityGrid);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Draws the geodesic grid plane as a separate overlay pass,\n"
                                   "independent from the 'Debug Mode' setting above.");
            }

            ImGui::Checkbox("Physics Collider Wireframe", &render.ShowPhysicsDebug);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Draws PhysX's own debug wireframe for every active collider\n"
                                   "(PxVisualizationParameter::eCOLLISION_SHAPES).");
            }

            ImGui::Checkbox("Object Path Trails", &render.ShowObjectPaths);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Draws each mesh's and sphere's recent world-space path while the\n"
                                   "simulation is playing (green for meshes, magenta for spheres).");
            }

            ImGui::End();
        }

    private:
        std::shared_ptr<UiState> m_UiState;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(DebugWindowLayer)
