//
// Created by leo on 12/28/25.
//

#include "GeneralRelativityWindow.h"
#include "ParameterWidgets.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include "imgui.h"

bool GeneralRelativityWindow::s_showAdvancedSettings = false;

void GeneralRelativityWindow::Render(bool* p_open) {
    ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("General Relativity Settings", p_open)) {
        ImGui::End();
        return;
    }

    // Header section with description
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
    ImGui::TextWrapped("Configure Kerr metric black hole physics and rendering parameters");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Master Control Section
    if (ImGui::CollapsingHeader("Master Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool kerrPhysicsEnabled = Application::Params().Get(Params::GRKerrPhysicsEnabled, true);

        if (ImGui::Checkbox("Enable Kerr Physics", &kerrPhysicsEnabled)) {
            Application::Params().Set(Params::GRKerrPhysicsEnabled, kerrPhysicsEnabled);
            Application::Instance().GetUI().MarkConfigDirty();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Master toggle for all Kerr metric relativistic effects.\n"
                "When disabled, falls back to simplified Schwarzschild approximation.\n"
                "Affects: frame-dragging, spin-dependent lensing, ISCO calculations."
            );
        }

        ImGui::SameLine();
        if (kerrPhysicsEnabled) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
            ImGui::Text("ACTIVE");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            ImGui::Text("INACTIVE");
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::TextDisabled(
            "Kerr physics enables accurate simulation of rotating black holes,\n"
            "including frame-dragging and spin-dependent gravitational lensing."
        );
    }

    ImGui::Spacing();

    // Lookup Tables Section
    if (ImGui::CollapsingHeader("Lookup Tables (LUTs)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextWrapped("Pre-computed geodesic data for real-time Kerr metric rendering");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        // Deflection LUT
        bool useDeflectionLUT = Application::Params().Get(Params::GRUseKerrDeflectionLUT, true);
        if (ImGui::Checkbox("Use Kerr Deflection LUT (3D)", &useDeflectionLUT)) {
            Application::Params().Set(Params::GRUseKerrDeflectionLUT, useDeflectionLUT);
            Application::Instance().GetUI().MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "3D lookup table: impact parameter × inclination × spin → deflection angle\n"
                "Resolution: 256×128×64 samples\n"
                "Accelerates photon geodesic calculations in Kerr spacetime.\n"
                "Disable for analytical calculation (slower but potentially more accurate)."
            );
        }

        // Redshift LUT
        bool useRedshiftLUT = Application::Params().Get(Params::GRUseKerrRedshiftLUT, true);
        if (ImGui::Checkbox("Use Kerr Redshift LUT (3D)", &useRedshiftLUT)) {
            Application::Params().Set(Params::GRUseKerrRedshiftLUT, useRedshiftLUT);
            Application::Instance().GetUI().MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "3D lookup table: impact parameter × inclination × spin → redshift factor\n"
                "Resolution: 256×128×64 samples\n"
                "Pre-computes gravitational and Doppler redshift for rotating black holes.\n"
                "Essential for accurate accretion disk color rendering."
            );
        }

        // Photon Sphere LUT
        bool usePhotonSphereLUT = Application::Params().Get(Params::GRUsePhotonSphereLUT, true);
        if (ImGui::Checkbox("Use Photon Sphere LUT (2D)", &usePhotonSphereLUT)) {
            Application::Params().Set(Params::GRUsePhotonSphereLUT, usePhotonSphereLUT);
            Application::Instance().GetUI().MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "2D lookup table: inclination × spin → photon sphere radius\n"
                "Resolution: 128×64 samples\n"
                "Defines the critical orbit radius where photons can orbit the black hole.\n"
                "Required for accurate Einstein ring rendering."
            );
        }

        // ISCO LUT
        bool useISCOLUT = Application::Params().Get(Params::GRUseISCOLUT, true);
        if (ImGui::Checkbox("Use ISCO LUT (1D)", &useISCOLUT)) {
            Application::Params().Set(Params::GRUseISCOLUT, useISCOLUT);
            Application::Instance().GetUI().MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "1D lookup table: spin → innermost stable circular orbit radius\n"
                "Resolution: 64 samples\n"
                "Uses Page-Thorne formula for accurate ISCO calculation.\n"
                "Determines inner edge of accretion disk (where matter plunges in)."
            );
        }

        ImGui::Spacing();
        ImGui::TextDisabled(
            "LUTs are generated at startup and cached in GPU memory.\n"
            "Toggle individual LUTs to compare quality vs. performance."
        );
    }

    ImGui::Spacing();

    // Lensing Effects Section
    if (ImGui::CollapsingHeader("Lensing Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextWrapped("Visual effects from strong gravitational lensing");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        // Einstein Rings
        bool showEinsteinRings = Application::Params().Get(Params::GRShowEinsteinRings, true);
        if (ImGui::Checkbox("Show Einstein Rings", &showEinsteinRings)) {
            Application::Params().Set(Params::GRShowEinsteinRings, showEinsteinRings);
            Application::Instance().GetUI().MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Display Einstein rings at photon sphere radius.\n"
                "Photons at this critical orbit can circle the black hole indefinitely.\n"
                "Creates distinctive ring-like structures in the lensed image."
            );
        }

        // Secondary Images
        bool showSecondaryImages = Application::Params().Get(Params::GRShowSecondaryImages, true);
        if (ImGui::Checkbox("Show Secondary Images", &showSecondaryImages)) {
            Application::Params().Set(Params::GRShowSecondaryImages, showSecondaryImages);
            Application::Instance().GetUI().MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Enable rendering of secondary (higher-order) lensed images.\n"
                "Light can orbit the black hole multiple times before reaching observer.\n"
                "Creates fainter duplicate images at larger angles."
            );
        }

        // Secondary Image Brightness
        ImGui::BeginDisabled(!showSecondaryImages);
        float secondaryBrightness = Application::Params().Get(Params::GRSecondaryImageBrightness, 0.3f);
        if (ImGui::SliderFloat("Secondary Brightness", &secondaryBrightness, 0.0f, 1.0f, "%.2f")) {
            Application::Params().Set(Params::GRSecondaryImageBrightness, secondaryBrightness);
            Application::Instance().GetUI().MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Brightness multiplier for secondary lensed images.\n"
                "Default: 0.3 (matches Interstellar movie rendering)\n"
                "Lower values: subtle effect, Higher values: more pronounced duplicates"
            );
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::TextDisabled(
            "Lensing effects are most visible when viewing background stars\n"
            "or accretion disk structures through strong gravitational fields."
        );
    }

    ImGui::Spacing();

    // Advanced Settings Section (Collapsible)
    if (ImGui::CollapsingHeader("Advanced Settings")) {
        s_showAdvancedSettings = true;

        // Physics Information
        if (ImGui::TreeNode("Physics Information")) {
            ImGui::BulletText("Metric: Kerr (rotating black hole)");
            ImGui::BulletText("Coordinates: Boyer-Lindquist");
            ImGui::BulletText("Integration: 4th-order Runge-Kutta (RK4)");
            ImGui::BulletText("Spin Range: 0.0 to 0.998 (near-extremal)");
            ImGui::Separator();
            ImGui::TextDisabled("Based on work by Kip Thorne et al.");
            ImGui::TextDisabled("Rendering techniques inspired by Interstellar (2014)");
            ImGui::TreePop();
        }

        // LUT Memory Usage
        if (ImGui::TreeNode("LUT Memory Usage")) {
            ImGui::BulletText("Deflection LUT: 256×128×64×4 = 8.4 MB");
            ImGui::BulletText("Redshift LUT: 256×128×64×4 = 8.4 MB");
            ImGui::BulletText("Photon Sphere LUT: 128×64×4 = 32 KB");
            ImGui::BulletText("ISCO LUT: 64×4 = 256 B");
            ImGui::Separator();
            ImGui::Text("Total GPU Memory: ~17 MB");
            ImGui::TreePop();
        }

        // Performance Notes
        if (ImGui::TreeNode("Performance Notes")) {
            ImGui::TextWrapped(
                "• LUT generation takes 10-30 seconds at startup\n"
                "• Real-time rendering: negligible performance impact\n"
                "• Analytical calculations (without LUTs): ~10-20× slower\n"
                "• LUTs are cached on GPU for instant access\n"
            );
            ImGui::TreePop();
        }
    } else {
        s_showAdvancedSettings = false;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Footer
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    ImGui::TextWrapped(
        "Settings are saved automatically to config.yaml and persist across sessions."
    );
    ImGui::PopStyleColor();

    ImGui::End();
}

