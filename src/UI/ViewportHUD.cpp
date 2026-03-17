#include "ViewportHUD.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/Camera.h"
#include "../Simulation/Scene.h"
#include "imgui.h"

#include <glm/glm.hpp>
#include <string>
#include <variant>
#include <vector>

namespace ViewportHUD {

static bool WorldToScreen(const glm::vec3& worldPos, const glm::mat4& viewProj,
                          float vpX, float vpY, float vpW, float vpH,
                          ImVec2& screenPos) {
    glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0f)
        return false;

    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f || ndc.z < -1.0f || ndc.z > 1.0f)
        return false;

    screenPos.x = vpX + (ndc.x * 0.5f + 0.5f) * vpW;
    screenPos.y = vpY + (1.0f - (ndc.y * 0.5f + 0.5f)) * vpH;
    return true;
}

static std::string Vec3Str(const glm::vec3& v) {
    char buf[64];
    snprintf(buf, sizeof(buf), "(%.1f, %.1f, %.1f)", v.x, v.y, v.z);
    return buf;
}

static std::string FloatStr(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

void Render(UI* ui, Scene* scene) {
    if (!scene || scene->objects.empty())
        return;

    auto& renderer = Application::GetRenderer();
    if (!renderer.camera)
        return;

    glm::mat4 viewProj = renderer.camera->GetViewProjectionMatrix();
    float vpX = renderer.m_viewportX;
    float vpY = renderer.m_viewportY;
    float vpW = renderer.m_viewportWidth;
    float vpH = renderer.m_viewportHeight;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoSavedSettings;

    ImGui::SetNextWindowPos(ImVec2(vpX, vpY));
    ImGui::SetNextWindowSize(ImVec2(vpW, vpH));

    if (!ImGui::Begin("##ViewportHUD", nullptr, flags)) {
        ImGui::End();
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList(ImGui::GetWindowViewport());
    drawList->PushClipRect(ImVec2(vpX, vpY), ImVec2(vpX + vpW, vpY + vpH));

    const ImU32 colBg = IM_COL32(20, 20, 20, 180);
    const ImU32 colBorder = IM_COL32(180, 100, 40, 200);
    const ImU32 colName = IM_COL32(255, 255, 255, 230);
    const ImU32 colLabel = IM_COL32(180, 100, 40, 230);
    const ImU32 colValue = IM_COL32(220, 220, 220, 220);
    const ImU32 colLine = IM_COL32(180, 100, 40, 120);
    const ImU32 colDot = IM_COL32(180, 100, 40, 230);

    constexpr float padding = 6.0f;
    constexpr float lineHeight = 16.0f;
    constexpr float labelWidth = 50.0f;
    constexpr float panelOffset = 14.0f;
    constexpr float dotRadius = 3.0f;

    for (size_t i = 0; i < scene->objects.size(); ++i) {
        auto& obj = scene->objects[i];

        auto posParam = obj.GetParameter(Field::Entity::Position);
        if (!std::holds_alternative<glm::vec3>(posParam))
            continue;

        glm::vec3 worldPos = std::get<glm::vec3>(posParam);

        ImVec2 screenPos;
        if (!WorldToScreen(worldPos, viewProj, vpX, vpY, vpW, vpH, screenPos))
            continue;

        auto nameParam = obj.GetParameter(Field::Entity::Name);
        std::string name = std::holds_alternative<std::string>(nameParam)
                           ? std::get<std::string>(nameParam)
                           : ("Object #" + std::to_string(i + 1));

        struct LabelLine {
            std::string label;
            std::string value;
        };
        std::vector<LabelLine> lines;

        lines.push_back({"Pos", Vec3Str(worldPos)});

        if (auto velParam = obj.GetParameter(Field::Physics::Velocity); std::holds_alternative<glm::vec3>(velParam)) {
            glm::vec3 vel = std::get<glm::vec3>(velParam);
            lines.push_back({"Vel", Vec3Str(vel)});
            lines.push_back({"Spd", FloatStr(glm::length(vel))});
        }

        if (auto massParam = obj.GetParameter(Field::Physics::Mass); std::holds_alternative<float>(massParam))
            lines.push_back({"Mass", FloatStr(std::get<float>(massParam))});

        if (obj.HasClass("BlackHole")) {
            if (auto spinParam = obj.GetParameter(Field::BlackHole::Spin); std::holds_alternative<float>(spinParam))
                lines.push_back({"Spin", FloatStr(std::get<float>(spinParam))});
        }

        if (obj.HasClass("Sphere")) {
            if (auto spinParam = obj.GetParameter(Field::Sphere::Spin); std::holds_alternative<float>(spinParam))
                lines.push_back({"Spin", FloatStr(std::get<float>(spinParam))});
        }

        float maxValueWidth = 0.0f;
        for (auto& line : lines) {
            ImVec2 sz = ImGui::CalcTextSize(line.value.c_str());
            if (sz.x > maxValueWidth)
                maxValueWidth = sz.x;
        }

        float panelWidth = padding * 2.0f + labelWidth + maxValueWidth;
        ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
        float nameRowWidth = padding * 2.0f + nameSize.x;
        if (nameRowWidth > panelWidth)
            panelWidth = nameRowWidth;

        float panelHeight = padding + lineHeight + padding * 0.5f
                          + static_cast<float>(lines.size()) * lineHeight + padding;

        float panelX = screenPos.x + panelOffset;
        float panelY = screenPos.y - panelHeight * 0.5f;

        if (panelX + panelWidth > vpX + vpW - 4.0f)
            panelX = screenPos.x - panelOffset - panelWidth;
        if (panelY < vpY + 4.0f)
            panelY = vpY + 4.0f;
        if (panelY + panelHeight > vpY + vpH - 4.0f)
            panelY = vpY + vpH - 4.0f - panelHeight;

        ImVec2 panelMin(panelX, panelY);
        ImVec2 panelMax(panelX + panelWidth, panelY + panelHeight);

        drawList->AddRectFilled(panelMin, panelMax, colBg, 4.0f);
        drawList->AddRect(panelMin, panelMax, colBorder, 4.0f, 0, 1.0f);

        drawList->AddCircleFilled(screenPos, dotRadius, colDot);

        ImVec2 lineStart = screenPos;
        ImVec2 lineEnd(panelX, panelY + panelHeight * 0.5f);
        if (panelX > screenPos.x)
            lineEnd.x = panelX;
        else
            lineEnd.x = panelX + panelWidth;
        drawList->AddLine(lineStart, lineEnd, colLine, 1.0f);

        float cursorY = panelY + padding;
        drawList->AddText(ImVec2(panelX + padding, cursorY), colName, name.c_str());
        cursorY += lineHeight + padding * 0.5f;

        for (auto& line : lines) {
            drawList->AddText(ImVec2(panelX + padding, cursorY), colLabel, line.label.c_str());
            drawList->AddText(ImVec2(panelX + padding + labelWidth, cursorY), colValue, line.value.c_str());
            cursorY += lineHeight;
        }
    }

    drawList->PopClipRect();
    ImGui::End();
}

}

