//
// Created by leo on 11/28/25.
//

#include "RendererWindow.h"
#include "ParameterWidgets.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include "../Renderer/Renderer.h"
#include "imgui.h"

namespace RendererWindow {

static float s_frameTimeHistory[60] = {};
static int s_frameTimeIndex = 0;
static float s_frameTimeAccum = 0.0f;
static int s_frameTimeAccumCount = 0;

static void RenderSectionHeader(const char* title) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
    ImGui::Text("%s", title);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

void Render(UI* ui, float fps) {
    if (!ImGui::Begin("System")) {
        ImGui::End();
        return;
    }

    RenderSectionHeader("PERFORMANCE");

    float frameTime = fps > 0.0f ? 1000.0f / fps : 0.0f;

    s_frameTimeAccum += frameTime;
    s_frameTimeAccumCount++;
    if (s_frameTimeAccumCount >= 2) {
        s_frameTimeHistory[s_frameTimeIndex] = s_frameTimeAccum / static_cast<float>(s_frameTimeAccumCount);
        s_frameTimeIndex = (s_frameTimeIndex + 1) % 60;
        s_frameTimeAccum = 0.0f;
        s_frameTimeAccumCount = 0;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 0.5f));
    ImGui::BeginChild("PerfPanel", ImVec2(0, 200), true);

    if (ImGui::BeginTable("FPSTable", 2)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f), "FPS");
        ImGui::TableNextColumn();
        ImVec4 fpsColor;
        if (fps >= 55.0f) fpsColor = ImVec4(0.2f, 0.9f, 0.2f, 1.0f);
        else if (fps >= 30.0f) fpsColor = ImVec4(0.9f, 0.9f, 0.2f, 1.0f);
        else fpsColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
        ImGui::PushFont(nullptr);
        ImGui::TextColored(fpsColor, "%.0f", fps);
        ImGui::PopFont();

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Frame Time");
        ImGui::TableNextColumn();
        ImGui::Text("%.2fms", frameTime);

        ImGui::EndTable();
    }

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.06f, 0.06f, 0.06f, 0.5f));
    ImGui::PlotHistogram("##FrameTimeChart", s_frameTimeHistory, 60, s_frameTimeIndex,
                         nullptr, 0.0f, 33.3f, ImVec2(-1, 60));
    ImGui::PopStyleColor(2);

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    
    auto& renderer = Application::GetRenderer();

    RenderSectionHeader("VIEWPORT SETTINGS");
    
    // Viewport Mode Selector
    const char* viewportModes[] = {
        "Demo 1 (Legacy)",
        "2D Rays",
        "3D Simulation (Raster + Compute)",
        "Hardware Ray Tracing (Vulkan RT)"
    };
    
    int currentMode = static_cast<int>(renderer.GetSelectedViewport());
    if (ImGui::Combo("Viewport Mode", &currentMode, viewportModes, IM_ARRAYSIZE(viewportModes))) {
        renderer.SetSelectedViewport(static_cast<Renderer::ViewportMode>(currentMode));
        ui->MarkConfigDirty();
    }
    
    if (renderer.GetSelectedViewport() == Renderer::ViewportMode::HardwareRayTracing) {
        if (auto rt = renderer.GetRayTracingRenderer()) {
            rt->RenderUI();
        }
    }
    
    ImGui::Spacing();

    bool vsync = Application::Params().Get(Params::WindowVSync, true);
    if (ImGui::Checkbox("VSync", &vsync)) {
        Application::Params().Set(Params::WindowVSync, vsync);
        ui->MarkConfigDirty();
    }

    RenderSectionHeader("SYSTEM INFO");

    if (ImGui::BeginTable("SystemInfo", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "GPU");
        ImGui::TableNextColumn();
        // ImGui::TextWrapped("%s", renderer.GetGPUName().c_str());
        ImGui::Text("Vulkan Device");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Vendor");
        ImGui::TableNextColumn();
        // ImGui::Text("%s", renderer.GetGPUVendor().c_str());
        ImGui::Text("N/A");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Resolution");
        ImGui::TableNextColumn();
        ImGui::Text("%dx%d", static_cast<int>(renderer.m_viewportWidth),
                    static_cast<int>(renderer.m_viewportHeight));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Renderer");
        ImGui::TableNextColumn();
        ImGui::Text("Vulkan Backend");

        ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("Memory Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto [totalAllocations, totalDeallocations, currentUsedMemory, peakMemory] = Memory::GetStats();
        auto& heapAllocator = Memory::GetHeapAllocator();

        ImGui::Text("Total Allocations: %zu", totalAllocations);
        ImGui::Text("Total Deallocations: %zu", totalDeallocations);
        ImGui::Text("Current Used: %zu bytes (%.2f MB)",
                    currentUsedMemory,
                    currentUsedMemory / (1024.0 * 1024.0));
        ImGui::Text("Peak Memory: %zu bytes (%.2f MB)",
                    peakMemory,
                    peakMemory / (1024.0 * 1024.0));

        ImGui::Separator();
        ImGui::Text("Heap Allocator:");
        ImGui::Indent();
        ImGui::Text("Used: %zu bytes (%.2f MB)",
                    heapAllocator.GetUsedMemory(),
                    heapAllocator.GetUsedMemory() / (1024.0 * 1024.0));
        ImGui::Text("Total: %zu bytes (%.2f MB)",
                    heapAllocator.GetTotalMemory(),
                    heapAllocator.GetTotalMemory() / (1024.0 * 1024.0));
        ImGui::Unindent();
    }

    ImGui::End();
}

} // namespace RendererWindow
