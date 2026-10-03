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
        const glm::vec3 front = glm::normalize(glm::vec3(
            std::cos(glm::radians(state.CameraYaw)) * std::cos(glm::radians(state.CameraPitch)),
            std::sin(glm::radians(state.CameraPitch)),
            std::sin(glm::radians(state.CameraYaw)) * std::cos(glm::radians(state.CameraPitch))));
        const glm::vec3 up{0.0f, 1.0f, 0.0f};
        glm::mat4 projection = glm::perspective(glm::radians(state.CameraFov), aspect, 0.1f, 10000.0f);
        projection[1][1] *= -1.0f;
        return projection * glm::lookAt(state.CameraPosition, state.CameraPosition + front, up);
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
            if (!m_UiState->ShowViewportHud) return;
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
            Scene& scene = *sceneLock;

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
