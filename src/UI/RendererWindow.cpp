//
// Created by leo on 11/28/25.
//

#include "RendererWindow.h"
#include "ParameterWidgets.h"
#include "../Application/UI.h"
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

    bool vsync = Application::Params().Get(Params::WindowVSync, true);
    if (ImGui::Checkbox("VSync", &vsync)) {
        Application::Params().Set(Params::WindowVSync, vsync);
        ui->MarkConfigDirty();
    }

    RenderSectionHeader("SYSTEM INFO");

    auto& renderer = Application::GetRenderer();

    if (ImGui::BeginTable("SystemInfo", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "GPU");
        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", renderer.GetGPUName().c_str());

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Vendor");
        ImGui::TableNextColumn();
        ImGui::Text("%s", renderer.GetGPUVendor().c_str());

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
        ImGui::Text("OpenGL %s", renderer.GetGLVersion().c_str());

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace RendererWindow
