#include <imgui.h>

import GPP;
import MoleHole;
import glm;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    glm::mat4 BuildViewProjection(const UiState& state, float aspect)
    {
        glm::mat4 projection = glm::perspective(glm::radians(state.ViewFov), aspect, 0.1f, 10000.0f);
        return projection * glm::lookAt(state.ViewPosition, state.ViewPosition + state.ViewFront, state.ViewUp);
    }

    bool WorldToScreen(const glm::vec3& worldPos, const glm::mat4& viewProj, const glm::vec2& min,
                       const glm::vec2& max, ImVec2& screenPos)
    {
        const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0f) return false;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f || ndc.z < -1.0f || ndc.z > 1.0f)
            return false;
        const float w = max.x - min.x;
        const float h = max.y - min.y;
        screenPos.x = min.x + (ndc.x * 0.5f + 0.5f) * w;
        screenPos.y = min.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * h;
        return true;
    }

    bool ProjectLine(glm::vec4 a, glm::vec4 b, const glm::vec2& min, const glm::vec2& max, ImVec2& outA, ImVec2& outB)
    {
        constexpr float kNear = 0.01f;
        if (a.w < kNear && b.w < kNear) return false;
        if (a.w < kNear) a = glm::mix(a, b, (kNear - a.w) / (b.w - a.w));
        else if (b.w < kNear) b = glm::mix(b, a, (kNear - b.w) / (a.w - b.w));
        const auto toScreen = [&](const glm::vec4& c)
        {
            const glm::vec2 ndc = glm::vec2(c) / c.w;
            return ImVec2(min.x + (ndc.x * 0.5f + 0.5f) * (max.x - min.x),
                          min.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * (max.y - min.y));
        };
        outA = toScreen(a);
        outB = toScreen(b);
        return true;
    }

    void DrawCameraFrustum(ImDrawList* list, const TransformComponent& transform, const CameraComponent& camera,
                           const glm::mat4& viewProj, const glm::vec2& min, const glm::vec2& max, float aspect,
                           ImU32 color, float thickness)
    {
        const glm::quat rotation = glm::normalize(transform.Rotation);
        const glm::vec3 right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 front = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        constexpr float kDepth = 2.0f;
        const float halfH = std::tan(glm::radians(camera.Fov) * 0.5f) * kDepth;
        const float halfW = halfH * aspect;
        const glm::vec3 center = transform.Position + front * kDepth;
        const glm::vec3 corners[4] = {center + right * halfW + up * halfH, center - right * halfW + up * halfH,
                                      center - right * halfW - up * halfH, center + right * halfW - up * halfH};
        const glm::vec3 apex = transform.Position;
        const glm::vec3 tip = center + up * (halfH * 1.35f);

        const auto line = [&](const glm::vec3& a, const glm::vec3& b)
        {
            ImVec2 pa, pb;
            if (ProjectLine(viewProj * glm::vec4(a, 1.0f), viewProj * glm::vec4(b, 1.0f), min, max, pa, pb))
                list->AddLine(pa, pb, color, thickness);
        };
        for (int i = 0; i < 4; ++i)
        {
            line(apex, corners[i]);
            line(corners[i], corners[(i + 1) % 4]);
        }
        line(corners[0], tip);
        line(corners[1], tip);
        line(apex, center);
    }

    struct ViewportHudLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, SceneManager, UiState>;

        ViewportHudLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<SceneManager> scenes,
                         std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_Scenes(std::move(scenes)), m_UiState(std::move(uiState))
        {
        }

        void OnUiRender() override
        {
            if (!m_UiState->ShowViewportHud && !m_UiState->ShowCameraGizmos) return;
            if (m_UiState->CurrentSceneName.empty()) return;
            const auto runner = m_Scenes->GetSimulation(m_UiState->CurrentSceneName);
            if (!runner) return;

            const glm::vec2 min = m_UiState->ViewportScreenMin;
            const glm::vec2 max = m_UiState->ViewportScreenMax;
            const float width = max.x - min.x;
            const float height = max.y - min.y;
            if (width <= 1.0f || height <= 1.0f) return;

            const glm::mat4 viewProj = BuildViewProjection(*m_UiState, width / height);

            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            drawList->PushClipRect(ImVec2(min.x, min.y), ImVec2(max.x, max.y));

            constexpr ImU32 colBg = IM_COL32(20, 20, 20, 180);
            constexpr ImU32 colBorder = IM_COL32(180, 100, 40, 200);
            constexpr ImU32 colName = IM_COL32(255, 255, 255, 230);
            constexpr ImU32 colDot = IM_COL32(180, 100, 40, 230);
            constexpr float padding = 6.0f;
            constexpr float lineHeight = 16.0f;

            auto sceneLock = runner->LockRenderScene();
            const Scene& scene = *sceneLock;

            if (m_UiState->ShowCameraGizmos)
            {
                for (auto [entity, transform, camera, metadata] :
                     scene.Registry().view<const TransformComponent, const CameraComponent, const MetadataComponent>().each())
                {
                    if (camera.Primary && m_UiState->SceneCameraActive) continue;
                    const bool selected = metadata.Guid == m_UiState->SelectedEntityGuid;
                    const ImU32 color = selected ? IM_COL32(255, 190, 90, 255)
                                        : camera.Primary ? IM_COL32(230, 140, 50, 220)
                                                         : IM_COL32(150, 170, 200, 200);
                    DrawCameraFrustum(drawList, transform, camera, viewProj, min, max, width / height, color,
                                      selected ? 2.0f : 1.4f);
                }
            }

            if (!m_UiState->ShowViewportHud)
            {
                drawList->PopClipRect();
                return;
            }

            for (auto [entity, transform, metadata] :
                 scene.Registry().view<const TransformComponent, const MetadataComponent>().each())
            {
                ImVec2 screenPos;
                if (!WorldToScreen(transform.Position, viewProj, min, max, screenPos)) continue;

                std::vector<std::string> lines;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "(%.1f, %.1f, %.1f)", transform.Position.x,
                             transform.Position.y, transform.Position.z);
                lines.emplace_back(buf);

                if (const auto* blackHole = scene.Registry().try_get<const BlackHoleComponent>(entity))
                {
                    std::snprintf(buf, sizeof(buf), "mass %.2f  spin %.2f", blackHole->Mass, blackHole->Spin);
                    lines.emplace_back(buf);
                }
                if (const auto* body = scene.Registry().try_get<const RigidBodyComponent>(entity))
                {
                    std::snprintf(buf, sizeof(buf), "mass %.2e kg", body->Mass);
                    lines.emplace_back(buf);
                }

                const std::string& name = metadata.Name.empty() ? metadata.TypeTag : metadata.Name;
                float panelWidth = padding * 2.0f + ImGui::CalcTextSize(name.c_str()).x;
                for (const auto& line : lines)
                    panelWidth = std::max(panelWidth, padding * 2.0f + ImGui::CalcTextSize(line.c_str()).x);
                const float panelHeight = padding * 2.0f + lineHeight * (1.0f + static_cast<float>(lines.size()));

                ImVec2 panelMin(screenPos.x + 14.0f, screenPos.y - panelHeight * 0.5f);
                ImVec2 panelMax(panelMin.x + panelWidth, panelMin.y + panelHeight);

                drawList->AddCircleFilled(screenPos, 3.0f, colDot);
                drawList->AddRectFilled(panelMin, panelMax, colBg, 4.0f);
                drawList->AddRect(panelMin, panelMax, colBorder, 4.0f, 0, 1.0f);

                float cursorY = panelMin.y + padding;
                drawList->AddText(ImVec2(panelMin.x + padding, cursorY), colName, name.c_str());
                cursorY += lineHeight;
                for (const auto& line : lines)
                {
                    drawList->AddText(ImVec2(panelMin.x + padding, cursorY), colName, line.c_str());
                    cursorY += lineHeight;
                }
            }

            drawList->PopClipRect();
        }

    private:
        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiState> m_UiState;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(ViewportHudLayer)
