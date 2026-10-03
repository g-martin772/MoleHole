import GPP;
import MoleHole;
import glm;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct CameraWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, UiState>;

        CameraWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_UiState(std::move(uiState))
        {
        }

        void OnUiRender() override
        {
            if (!m_UiState->ShowCameraWindow) return;
            if (!ImGui::Begin("Camera", &m_UiState->ShowCameraWindow))
            {
                ImGui::End();
                return;
            }

            SectionHeader("MOVEMENT");
            ImGui::SliderFloat("Speed", &m_UiState->CameraSpeed, 0.5f, 100.0f, "%.1f");
            ImGui::SliderFloat("Mouse Sensitivity", &m_UiState->CameraMouseSensitivity, 0.01f, 1.0f, "%.2f");

            SectionHeader("POSITION");
            ImGui::Text("Pos: (%.1f, %.1f, %.1f)", m_UiState->CameraPosition.x,
                        m_UiState->CameraPosition.y, m_UiState->CameraPosition.z);
            ImGui::Text("Yaw: %.1f  Pitch: %.1f", m_UiState->CameraYaw, m_UiState->CameraPitch);
            ImGui::SliderFloat("FOV", &m_UiState->CameraFov, 20.0f, 120.0f, "%.0f");

            ImGui::Spacing();
            if (ImGui::Button("Reset Camera", ImVec2(-1, 0)))
            {
                m_UiState->CameraPosition = glm::vec3(0.0f, 20.0f, 100.0f);
                m_UiState->CameraYaw = -90.0f;
                m_UiState->CameraPitch = 0.0f;
                m_UiState->CameraFov = 60.0f;
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Controls");
            ImGui::BulletText("WASD: Move");
            ImGui::BulletText("Q/E: Down/Up");
            ImGui::BulletText("Right Mouse + Drag: Look Around");

            ImGui::End();
        }

    private:
        std::shared_ptr<UiState> m_UiState;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(CameraWindowLayer)
