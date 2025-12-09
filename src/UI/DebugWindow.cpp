//
// Created by leo on 11/28/25.
//

#include "DebugWindow.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Renderer/PhysicsDebugRenderer.h"
#include "imgui.h"

#ifndef _DEBUG
#define _DEBUG
#endif
#include <PxPhysicsAPI.h>

using namespace physx;

namespace DebugWindow {

void RenderDebugModeCombo(UI* ui) {
    const char* debugModeItems[] = {
        "Normal Rendering",
        "Influence Zones",
        "Deflection Magnitude",
        "Gravitational Field",
        "Spherical Shape",
        "LUT Visualization",
        "Gravity Grid"
    };

    int debugMode = Application::State().GetInt(StateParameter::RenderingDebugMode);
    if (ImGui::Combo("Debug Mode", &debugMode, debugModeItems, IM_ARRAYSIZE(debugModeItems))) {
        Application::State().SetInt(StateParameter::RenderingDebugMode, debugMode);
        ui->MarkConfigDirty();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        RenderDebugModeTooltip(debugMode);
    }
}

void RenderDebugModeTooltip(int debugMode) {
    const char* tooltip = "";
    switch (debugMode) {
        case 0:
            tooltip = "Normal rendering with no debug visualization";
            break;
        case 1:
            tooltip = "Red zones showing gravitational influence areas\nBrighter red = closer to black hole\nOnly shows outside event horizon safety zone";
            break;
        case 2:
            tooltip = "Yellow/orange visualization of deflection strength\nBrightness indicates how much light rays are bent\nHelps visualize Kerr distortion effects";
            break;
        case 3:
            tooltip = "Green visualization of gravitational field strength\nBrighter green = stronger gravitational effects\nShows field within 10x Schwarzschild radius";
            break;
        case 4:
            tooltip = "Blue gradient showing black hole's spherical shape\nBlack interior = event horizon (no escape)\nBlue gradient = distance from event horizon\nHelps verify proper sphere geometry";
            break;
        case 5:
            tooltip = "Visualize the distortion lookup table (LUT)\n2D slice of the 3D LUT used for ray deflection\nHue encodes deflection direction, brightness encodes distance\nMagenta tint indicates invalid/overflow entries";
            break;
        case 6:
            tooltip = "Gravity Grid overlay on ground plane\nColor shows dominant black hole per cell (by mass/distance^2)\nGrid helps visualize regions of influence";
            break;
        default:
            tooltip = "Unknown debug mode";
            break;
    }
    ImGui::SetTooltip("%s", tooltip);
}

void Render(UI* ui) {
    if (!ImGui::Begin("Debug")) {
        ImGui::End();
        return;
    }

    auto& config = Application::State();
    
    // Rendering Flags Section
    if (ImGui::CollapsingHeader("Rendering Flags", ImGuiTreeNodeFlags_DefaultOpen)) {
        // General rendering flags
        ImGui::Text("General Settings");
        ImGui::Separator();
        
        bool renderBlackHoles = config.GetInt(StateParameter::RenderingBlackHolesEnabled) != 0;
        if (ImGui::Checkbox("Render Black Holes", &renderBlackHoles)) {
            config.SetInt(StateParameter::RenderingBlackHolesEnabled, renderBlackHoles ? 1 : 0);
            ui->MarkConfigDirty();
        }
        
        bool gravitationalLensing = config.GetInt(StateParameter::RenderingGravitationalLensingEnabled) != 0;
        if (ImGui::Checkbox("Gravitational Lensing", &gravitationalLensing)) {
            config.SetInt(StateParameter::RenderingGravitationalLensingEnabled, gravitationalLensing ? 1 : 0);
            ui->MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable/disable gravitational light bending effects");
        }
        
        bool gravitationalRedshift = config.GetInt(StateParameter::RenderingGravitationalRedshiftEnabled) != 0;
        if (ImGui::Checkbox("Gravitational Redshift", &gravitationalRedshift)) {
            config.SetInt(StateParameter::RenderingGravitationalRedshiftEnabled, gravitationalRedshift ? 1 : 0);
            ui->MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable/disable gravitational redshift in accretion disk");
        }
        
        ImGui::Spacing();
        ImGui::Text("Accretion Disk Settings");
        ImGui::Separator();
        
        bool accretionDisk = config.GetInt(StateParameter::RenderingAccretionDiskEnabled) != 0;
        if (ImGui::Checkbox("Accretion Disk", &accretionDisk)) {
            config.SetInt(StateParameter::RenderingAccretionDiskEnabled, accretionDisk ? 1 : 0);
            ui->MarkConfigDirty();
        }
        
        float accDiskHeight = config.GetFloat(StateParameter::RenderingAccDiskHeight);
        if (ImGui::SliderFloat("Disk Height", &accDiskHeight, 0.01f, 2.0f)) {
            config.SetFloat(StateParameter::RenderingAccDiskHeight, accDiskHeight);
            ui->MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Vertical thickness of the accretion disk");
        }
        
        float accDiskNoiseScale = config.GetFloat(StateParameter::RenderingAccDiskNoiseScale);
        if (ImGui::SliderFloat("Noise Scale", &accDiskNoiseScale, 0.1f, 10.0f)) {
            config.SetFloat(StateParameter::RenderingAccDiskNoiseScale, accDiskNoiseScale);
            ui->MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Scale of the noise pattern in the accretion disk");
        }
        
        float accDiskNoiseLOD = config.GetFloat(StateParameter::RenderingAccDiskNoiseLOD);
        if (ImGui::SliderFloat("Noise LOD", &accDiskNoiseLOD, 1.0f, 10.0f)) {
            config.SetFloat(StateParameter::RenderingAccDiskNoiseLOD, accDiskNoiseLOD);
            ui->MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Level of detail for noise (more octaves = more detail)");
        }
        
        float accDiskSpeed = config.GetFloat(StateParameter::RenderingAccDiskSpeed);
        if (ImGui::SliderFloat("Disk Rotation Speed", &accDiskSpeed, 0.0f, 5.0f)) {
            config.SetFloat(StateParameter::RenderingAccDiskSpeed, accDiskSpeed);
            ui->MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Rotation speed of the accretion disk animation");
        }
        
        bool dopplerBeaming = config.GetInt(StateParameter::RenderingDopplerBeamingEnabled) != 0;
        if (ImGui::Checkbox("Doppler Beaming", &dopplerBeaming)) {
            config.SetInt(StateParameter::RenderingDopplerBeamingEnabled, dopplerBeaming ? 1 : 0);
            ui->MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enable relativistic Doppler shift and beaming effects");
        }
    }

    // Debug Visualization Section
    if (ImGui::CollapsingHeader("Debug Visualization", ImGuiTreeNodeFlags_DefaultOpen)) {
        RenderDebugModeCombo(ui);

        if (static_cast<DebugMode>(Application::State().GetInt(StateParameter::RenderingDebugMode)) == DebugMode::GravityGrid) {
            auto& renderer = Application::GetRenderer();
            if (auto* grid = renderer.GetGravityGridRenderer()) {
                ImGui::Separator();
                ImGui::TextDisabled("Gravity Grid (Plane) Settings");

                float planeY = grid->GetPlaneY();
                if (ImGui::DragFloat("Plane Y", &planeY, 0.5f, -10000.0f, 10000.0f)) {
                    grid->SetPlaneY(planeY);
                }
                float size = grid->GetPlaneSize();
                if (ImGui::DragFloat("Plane Size", &size, 1.0f, 2.0f, 10000.0f)) {
                    grid->SetPlaneSize(size);
                }
                int res = grid->GetResolution();
                if (ImGui::SliderInt("Resolution", &res, 8, 512)) {
                    grid->SetResolution(res);
                }
                float cellSize = grid->GetCellSize();
                if (ImGui::DragFloat("Grid Cell Size", &cellSize, 0.05f, 0.01f, 100.0f)) {
                    grid->SetCellSize(cellSize);
                }
                float lineThickness = grid->GetLineThickness();
                if (ImGui::DragFloat("Line Thickness (cells)", &lineThickness, 0.005f, 0.001f, 0.5f)) {
                    grid->SetLineThickness(lineThickness);
                }
                float opacity = grid->GetOpacity();
                if (ImGui::SliderFloat("Opacity", &opacity, 0.05f, 1.0f)) {
                    grid->SetOpacity(opacity);
                }

                glm::vec3 color = grid->GetColor();
                if (ImGui::ColorEdit3("Grid Color", &color[0])) {
                    grid->SetColor(color);
                }
            }
        }
    }

    // PhysX Visualization Section
    if (ImGui::CollapsingHeader("PhysX Visualization")) {
        auto& renderer = Application::GetRenderer();
        auto* physicsDebugRenderer = renderer.GetPhysicsDebugRenderer();

        if (!physicsDebugRenderer) {
            ImGui::TextDisabled("PhysX debug renderer not available");
            ImGui::End();
            return;
        }

        auto& simulation = Application::GetSimulation();
        bool simRunning = simulation.IsRunning();

        if (!simRunning) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
            ImGui::TextWrapped("Note: Start the simulation to see PhysX debug visualization!");
            ImGui::PopStyleColor();
            ImGui::Separator();
        }

        bool enabled = config.GetBool(StateParameter::RenderingPhysicsDebugEnabled);
        if (ImGui::Checkbox("Enable PhysX Debug Rendering", &enabled)) {
            config.SetBool(StateParameter::RenderingPhysicsDebugEnabled, enabled);
            physicsDebugRenderer->SetEnabled(enabled);
            ui->MarkConfigDirty();
        }

        if (!enabled) {
            ImGui::BeginDisabled();
        }

        bool depthTest = config.GetBool(StateParameter::RenderingPhysicsDebugDepthTest);
        if (ImGui::Checkbox("Depth Test", &depthTest)) {
            config.SetBool(StateParameter::RenderingPhysicsDebugDepthTest, depthTest);
            physicsDebugRenderer->SetDepthTestEnabled(depthTest);
            ui->MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("When enabled, debug geometry respects depth. When disabled, it draws on top.");
        }

        float scale = config.GetFloat(StateParameter::RenderingPhysicsDebugScale);
        if (ImGui::SliderFloat("Visualization Scale", &scale, 0.1f, 20.0f)) {
            config.SetFloat(StateParameter::RenderingPhysicsDebugScale, scale);
            if (simulation.GetPhysics()) {
                simulation.GetPhysics()->SetVisualizationScale(scale);
            }
            ui->MarkConfigDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Adjust the size of debug visualization geometry. Increase if you can't see anything.");
        }

        // Show diagnostic info
        if (simulation.GetPhysics()) {
            const PxRenderBuffer* renderBuffer = simulation.GetPhysics()->GetDebugRenderBuffer();
            if (renderBuffer) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Debug Primitives:");
                ImGui::Text("  Lines: %u", renderBuffer->getNbLines());
                ImGui::Text("  Triangles: %u", renderBuffer->getNbTriangles());
                ImGui::Text("  Points: %u", renderBuffer->getNbPoints());
                if (renderBuffer->getNbLines() == 0 && renderBuffer->getNbTriangles() == 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "  No geometry! Check flags below.");
                }
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Visualization Flags");

        uint32_t flags = static_cast<uint32_t>(config.GetInt(StateParameter::RenderingPhysicsDebugFlags));
        bool flagsChanged = false;

        ImGui::Text("Quick Presets:");
        if (ImGui::Button("Show All Collision")) {
            flags = (1 << 11) | (1 << 16) | (1 << 17);
            flagsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Show Everything")) {
            flags = 0xFFFFFFFF;
            flagsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear All")) {
            flags = 0;
            flagsChanged = true;
        }

        ImGui::Separator();

        if (ImGui::TreeNode("World")) {
            bool worldAxes = flags & (1 << 0);
            if (ImGui::Checkbox("World Axes", &worldAxes)) {
                flags = worldAxes ? (flags | (1 << 0)) : (flags & ~(1 << 0));
                flagsChanged = true;
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Body Properties")) {
            bool bodyAxes = flags & (1 << 1);
            if (ImGui::Checkbox("Body Axes", &bodyAxes)) {
                flags = bodyAxes ? (flags | (1 << 1)) : (flags & ~(1 << 1));
                flagsChanged = true;
            }

            bool massAxes = flags & (1 << 2);
            if (ImGui::Checkbox("Mass Axes", &massAxes)) {
                flags = massAxes ? (flags | (1 << 2)) : (flags & ~(1 << 2));
                flagsChanged = true;
            }

            bool linVelocity = flags & (1 << 3);
            if (ImGui::Checkbox("Linear Velocity", &linVelocity)) {
                flags = linVelocity ? (flags | (1 << 3)) : (flags & ~(1 << 3));
                flagsChanged = true;
            }

            bool angVelocity = flags & (1 << 4);
            if (ImGui::Checkbox("Angular Velocity", &angVelocity)) {
                flags = angVelocity ? (flags | (1 << 4)) : (flags & ~(1 << 4));
                flagsChanged = true;
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Contact Info")) {
            bool contactPoint = flags & (1 << 5);
            if (ImGui::Checkbox("Contact Points", &contactPoint)) {
                flags = contactPoint ? (flags | (1 << 5)) : (flags & ~(1 << 5));
                flagsChanged = true;
            }

            bool contactNormal = flags & (1 << 6);
            if (ImGui::Checkbox("Contact Normals", &contactNormal)) {
                flags = contactNormal ? (flags | (1 << 6)) : (flags & ~(1 << 6));
                flagsChanged = true;
            }

            bool contactError = flags & (1 << 7);
            if (ImGui::Checkbox("Contact Error", &contactError)) {
                flags = contactError ? (flags | (1 << 7)) : (flags & ~(1 << 7));
                flagsChanged = true;
            }

            bool contactForce = flags & (1 << 8);
            if (ImGui::Checkbox("Contact Force", &contactForce)) {
                flags = contactForce ? (flags | (1 << 8)) : (flags & ~(1 << 8));
                flagsChanged = true;
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Actor")) {
            bool actorAxes = flags & (1 << 9);
            if (ImGui::Checkbox("Actor Axes", &actorAxes)) {
                flags = actorAxes ? (flags | (1 << 9)) : (flags & ~(1 << 9));
                flagsChanged = true;
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Collision Shapes")) {
            bool collAABBs = flags & (1 << 10);
            if (ImGui::Checkbox("Collision AABBs", &collAABBs)) {
                flags = collAABBs ? (flags | (1 << 10)) : (flags & ~(1 << 10));
                flagsChanged = true;
            }

            bool collShapes = flags & (1 << 11);
            if (ImGui::Checkbox("Collision Shapes", &collShapes)) {
                flags = collShapes ? (flags | (1 << 11)) : (flags & ~(1 << 11));
                flagsChanged = true;
            }

            bool collAxes = flags & (1 << 12);
            if (ImGui::Checkbox("Collision Axes", &collAxes)) {
                flags = collAxes ? (flags | (1 << 12)) : (flags & ~(1 << 12));
                flagsChanged = true;
            }

            bool collCompounds = flags & (1 << 13);
            if (ImGui::Checkbox("Collision Compounds", &collCompounds)) {
                flags = collCompounds ? (flags | (1 << 13)) : (flags & ~(1 << 13));
                flagsChanged = true;
            }

            bool collFNormals = flags & (1 << 14);
            if (ImGui::Checkbox("Face Normals", &collFNormals)) {
                flags = collFNormals ? (flags | (1 << 14)) : (flags & ~(1 << 14));
                flagsChanged = true;
            }

            bool collEdges = flags & (1 << 15);
            if (ImGui::Checkbox("Collision Edges", &collEdges)) {
                flags = collEdges ? (flags | (1 << 15)) : (flags & ~(1 << 15));
                flagsChanged = true;
            }

            bool collStatic = flags & (1 << 16);
            if (ImGui::Checkbox("Static Shapes", &collStatic)) {
                flags = collStatic ? (flags | (1 << 16)) : (flags & ~(1 << 16));
                flagsChanged = true;
            }

            bool collDynamic = flags & (1 << 17);
            if (ImGui::Checkbox("Dynamic Shapes", &collDynamic)) {
                flags = collDynamic ? (flags | (1 << 17)) : (flags & ~(1 << 17));
                flagsChanged = true;
            }
            ImGui::TreePop();
        }

        if (flagsChanged) {
            config.SetInt(StateParameter::RenderingPhysicsDebugFlags, static_cast<int>(flags));
            if (simulation.GetPhysics()) {
                auto* physics = simulation.GetPhysics();
                physics->SetVisualizationParameter(PxVisualizationParameter::eWORLD_AXES, (flags & (1 << 0)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eBODY_AXES, (flags & (1 << 1)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eBODY_MASS_AXES, (flags & (1 << 2)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eBODY_LIN_VELOCITY, (flags & (1 << 3)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eBODY_ANG_VELOCITY, (flags & (1 << 4)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eCONTACT_POINT, (flags & (1 << 5)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eCONTACT_NORMAL, (flags & (1 << 6)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eCONTACT_ERROR, (flags & (1 << 7)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eCONTACT_FORCE, (flags & (1 << 8)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eACTOR_AXES, (flags & (1 << 9)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_AABBS, (flags & (1 << 10)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, (flags & (1 << 11)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_AXES, (flags & (1 << 12)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_COMPOUNDS, (flags & (1 << 13)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_FNORMALS, (flags & (1 << 14)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_EDGES, (flags & (1 << 15)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_STATIC, (flags & (1 << 16)) ? 1.0f : 0.0f);
                physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_DYNAMIC, (flags & (1 << 17)) ? 1.0f : 0.0f);
            }
            ui->MarkConfigDirty();
        }

        if (!enabled) {
            ImGui::EndDisabled();
        }
    }

    ImGui::End();
}

} // namespace DebugWindow
