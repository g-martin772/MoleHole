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

            ImFont* icons = m_UiState->IconFont;
            const auto slider = [&](const char* label, float& value, float fallback, float min, float max,
                                    const char* format)
            {
                if (PropertyRow(icons, label, value != fallback, [&]
                {
                    DragFloatValue("##v", &value, (max - min) / 400.0f, min, max, format);
                }))
                {
                    value = fallback;
                }
            };

            if (BeginSection(icons, "Movement"))
            {
                slider("Speed", m_UiState->CameraSpeed, 5.0f, 0.5f, 100.0f, "%.1f");
                slider("Mouse Sensitivity", m_UiState->CameraMouseSensitivity, 0.1f, 0.01f, 1.0f, "%.2f");
                EndSection();
            }

            if (BeginSection(icons, "Position"))
            {
                ImGui::Text("Pos: (%.1f, %.1f, %.1f)", m_UiState->CameraPosition.x,
                            m_UiState->CameraPosition.y, m_UiState->CameraPosition.z);
                ImGui::Text("Yaw: %.1f  Pitch: %.1f", m_UiState->CameraYaw, m_UiState->CameraPitch);
                slider("FOV", m_UiState->CameraFov, 60.0f, 20.0f, 120.0f, "%.0f");
                EndSection();
            }

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
