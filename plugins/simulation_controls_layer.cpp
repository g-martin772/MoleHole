import GPP;
import MoleHole;
import glm;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    constexpr const char* kIconPlay = "\xef\x81\x8b"; // U+F04B
    constexpr const char* kIconPause = "\xef\x81\x8c"; // U+F04C
    constexpr const char* kIconStop = "\xef\x81\x8d"; // U+F04D

    struct SimulationControlsLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, SceneManager, UiState>;

        SimulationControlsLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<SceneManager> scenes,
                                std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_Scenes(std::move(scenes)), m_UiState(std::move(uiState))
        {
        }

        void OnAttach() override
        {
            EnsureIconFont(*m_UiState);
        }

        void OnUiRender() override
        {
            if (m_UiState->CurrentSceneName.empty()) return;
            const auto runner = m_Scenes->GetSimulation(m_UiState->CurrentSceneName);
            if (!runner) return;

            const glm::vec2 min = m_UiState->ViewportScreenMin;
            const glm::vec2 max = m_UiState->ViewportScreenMax;
            if (max.x - min.x < 1.0f || max.y - min.y < 1.0f) return;

            constexpr float buttonSize = 48.0f;

            ImGui::SetNextWindowPos(ImVec2((min.x + max.x) * 0.5f, min.y + 16.0f), ImGuiCond_Always,
                                    ImVec2(0.5f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.08f, 0.85f));

            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                               ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking |
                                               ImGuiWindowFlags_AlwaysAutoResize |
                                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings;

            if (ImGui::Begin("##SimulationControls", nullptr, flags))
            {
                const bool paused = runner->IsPaused();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(180.0f / 255.0f, 100.0f / 255.0f, 40.0f / 255.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                      ImVec4(200.0f / 255.0f, 120.0f / 255.0f, 50.0f / 255.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

                const bool hasIconFont = m_UiState->IconFont != nullptr;
                if (hasIconFont) ImGui::PushFont(m_UiState->IconFont);
                if (ImGui::Button(hasIconFont ? (paused ? kIconPlay : kIconPause) : (paused ? ">" : "||"),
                                  ImVec2(buttonSize, buttonSize)))
                {
                    runner->SetPaused(!paused);
                }
                if (hasIconFont) ImGui::PopFont();
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(paused ? "Resume" : "Pause");

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                if (hasIconFont) ImGui::PushFont(m_UiState->IconFont);
                if (ImGui::Button(hasIconFont ? kIconStop : "[]", ImVec2(buttonSize, buttonSize)))
                {
                    m_UiState->PendingStartPaused = true;
                    if (!m_UiState->CurrentScenePath.empty())
                        m_UiState->PendingLoadScenePath = m_UiState->CurrentScenePath;
                    else
                        m_UiState->PendingNewScene = true;
                }
                if (hasIconFont) ImGui::PopFont();
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(m_UiState->CurrentScenePath.empty()
                                          ? "Stop (unsaved scene -- resets to a fresh default)"
                                          : "Stop (reload from disk, paused)");
                }
            }
            ImGui::End();

            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }

    private:
        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiState> m_UiState;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(SimulationControlsLayer)
