//
// Created by leo on 11/28/25.
//

#include "RendererWindow.h"
#include "ParameterWidgets.h"
#include "../Application/Parameters.h"
#include "imgui.h"

namespace RendererWindow {

void Render(UI* ui, float fps) {
    if (!ImGui::Begin("System")) {
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("System Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Text("Frame time: %.3f ms", 1000.0f / fps);
    }

    ParameterWidgets::RenderParameterGroup(ParameterGroup::Window, ui,
                                          ParameterWidgets::WidgetStyle::Standard, true);

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
