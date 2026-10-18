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
                double speed = m_UiState->CameraSpeedMs;
                constexpr double kDefaultSpeedMs = 5.0 * Units::kSolarGravitationalRadius;
                if (PropertyRow(icons, "Speed", speed != kDefaultSpeedMs, [&]
                {
                    QuantityValue("##v", &speed, Units::Dimension::Velocity, 1.0e3, 1.0e-3, Units::kSpeedOfLight * 1.0e3);
                }))
                {
                    speed = kDefaultSpeedMs;
                }
                m_UiState->CameraSpeedMs = speed;
                slider("Mouse Sensitivity", m_UiState->CameraMouseSensitivity, 0.1f, 0.01f, 1.0f, "%.2f");
                EndSection();
            }

            if (BeginSection(icons, "Scene Camera"))
            {
                ImGui::Checkbox("Possess scene camera while playing", &m_UiState->PossessSceneCamera);
                ImGui::Checkbox("Preview scene camera in edit mode", &m_UiState->PreviewSceneCamera);
                ImGui::TextDisabled(m_UiState->SceneCameraActive ? "Viewing through the primary scene camera"
                                                                 : "Free-fly editor camera");
                EndSection();
            }

            if (BeginSection(icons, "Observer"))
            {
                DrawObserver(*m_UiState->Observer.Load());
                EndSection();
            }

            if (BeginSection(icons, "Position"))
            {
                glm::dvec3 position = m_UiState->CameraPositionM;
                PropertyRow(icons, "Position", false, [&]
                {
                    QuantityVecValue("##v", &position.x, 3, nullptr, Units::Dimension::Length, 1.0e3);
                });
                m_UiState->CameraPositionM = position;
                ImGui::Text("Yaw: %.1f  Pitch: %.1f", m_UiState->CameraYaw, m_UiState->CameraPitch);
                slider("FOV", m_UiState->CameraFov, 60.0f, 20.0f, 120.0f, "%.0f");
                EndSection();
            }

            ImGui::Spacing();
            if (ImGui::Button("Reset Camera", ImVec2(-1, 0)))
            {
                m_UiState->CameraPositionM = glm::dvec3(0.0, 20.0, 100.0) * Units::kSolarGravitationalRadius;
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
        // What the camera's observer measures; docs/notes/observers.md
        static void DrawObserver(const ObserverReadout& readout)
        {
            ImGui::TextWrapped("%s", readout.Status.c_str());
            if (!readout.Rendered) ImGui::TextDisabled("Only the Integrated transport renders the observer's motion.");
            if (readout.Source != ObserverSource::None && !readout.InsideHorizon)
            {
                ImGui::Text("r = %.6g M", readout.Radius);
                ImGui::Text("Speed vs ZAMO: %.4f c (γ = %.4f)", readout.Speed, readout.Gamma);
                ImGui::Text("Clock rate dτ/dt: %.6g", readout.ClockRate);
                const double dilation = readout.ClockRate > 0.0 ? 1.0 / readout.ClockRate
                                                                : std::numeric_limits<double>::infinity();
                ImGui::Text("1 h here = %s far away", FormatLongDuration(3600.0 * dilation).c_str());
                if (readout.Source == ObserverSource::Body)
                {
                    ImGui::Text("Proper time: %s", FormatLongDuration(readout.ProperTimeS).c_str());
                }
                if (readout.FreeFall)
                {
                    ImGui::Text("Proper acceleration: 0 (free fall)");
                }
                else if (readout.Source != ObserverSource::Body)
                {
                    ImGui::Text("Proper acceleration: %.3g m/s² (%.3g g⊕)", readout.AccelerationMs2,
                                readout.AccelerationMs2 / EarthSurfaceGravity());
                }
            }
            ImGui::TextDisabled("Choose the observer under Render > Camera > Observer, or give the primary scene camera "
                                "a Geodesic Body and possess it while playing.");
        }

        std::shared_ptr<UiState> m_UiState;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(CameraWindowLayer)
