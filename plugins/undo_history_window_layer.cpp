#include <imgui.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    constexpr std::size_t kMaxRows = 2048;

    struct UndoHistoryWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, SceneManager, UiState>;

        UndoHistoryWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<SceneManager> scenes,
                               std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_Scenes(std::move(scenes)), m_UiState(std::move(uiState))
        {
        }

        void OnUiRender() override
        {
            if (!m_UiState->ShowUndoHistoryWindow) return;
            ImGui::SetNextWindowSize(ImVec2(320.0f, 360.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Undo History", &m_UiState->ShowUndoHistoryWindow))
            {
                ImGui::End();
                return;
            }
            const auto runner = m_UiState->CurrentSceneName.empty() ? nullptr : m_Scenes->GetSimulation(m_UiState->CurrentSceneName);
            if (!runner)
            {
                ImGui::TextDisabled("No scene loaded");
                ImGui::End();
                return;
            }
            Draw(runner);
            ImGui::End();
        }

    private:
        void Draw(const std::shared_ptr<SimulationRunner>& runner)
        {
            if (m_Runner.lock() != runner || runner->HistoryVersion() != m_View.Version)
            {
                m_Runner = runner;
                m_View = runner->History(kMaxRows);
            }
            const bool playing = !runner->IsPaused();
            const auto now = std::chrono::steady_clock::now();
            const auto rows = BuildHistoryRows(m_View, now);

            ImGui::TextDisabled("%zu undo  |  %zu redo", m_View.UndoCount, m_View.RedoCount);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
            UndoCapacityInput("Capacity##undo", &m_UiState->UndoHistoryCapacity);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Maximum undo steps kept (also in Settings)");
            if (playing) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "Pause the simulation to undo or redo");

            const bool empty = m_View.UndoCount == 0 && m_View.RedoCount == 0;
            if (ImGui::Button("Clear History") && !empty) ImGui::OpenPopup("Clear Undo History");
            ImGui::Separator();

            ImGui::BeginChild("##historyRows", ImVec2(0.0f, 0.0f));
            std::optional<HistoryRow> jump;
            int index = 0;
            for (const auto& row : rows)
            {
                ImGui::PushID(index++);
                const bool clicked = ImGui::Selectable("##row", row.Current, ImGuiSelectableFlags_AllowOverlap);
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                auto* list = ImGui::GetWindowDrawList();
                const ImU32 text = ImGui::GetColorU32(row.Future || playing ? ImGuiCol_TextDisabled : ImGuiCol_Text);
                const float y = min.y + (max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f;
                if (row.Current) list->AddText(ImVec2(min.x + 4.0f, y), ImGui::GetColorU32(ImGuiCol_Text), ">");
                list->AddText(ImVec2(min.x + 20.0f, y), text, row.Label.c_str());
                const float ageWidth = ImGui::CalcTextSize(row.Age.c_str()).x;
                list->AddText(ImVec2(max.x - ageWidth - 6.0f, y), ImGui::GetColorU32(ImGuiCol_TextDisabled), row.Age.c_str());
                if (clicked && !row.Current) jump = row;
                if (row.Current && std::exchange(m_ScrollToCurrent, false)) ImGui::SetScrollHereY(0.5f);
                ImGui::PopID();
            }
            ImGui::EndChild();

            if (jump) JumpTo(*runner, *jump, playing);
            DrawClearPopup(*runner);
        }

        void JumpTo(SimulationRunner& runner, const HistoryRow& row, const bool playing)
        {
            if (playing)
            {
                m_Logger->Notifications()->Post(NotificationKind::Info,
                                                std::format("Pause the simulation to {}", row.Future ? "redo" : "undo"),
                                                {}, 0.0f, "history-blocked");
                return;
            }
            if (row.Future) runner.RedoSteps(row.Steps);
            else runner.UndoSteps(row.Steps);
            m_ScrollToCurrent = true;
        }

        void DrawClearPopup(SimulationRunner& runner)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (!ImGui::BeginPopupModal("Clear Undo History", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::Text("Discard all %zu undo and %zu redo steps?", m_View.UndoCount, m_View.RedoCount);
            ImGui::TextDisabled("The scene itself is not changed.");
            ImGui::Spacing();
            if (ImGui::Button("Clear", ImVec2(100, 0)))
            {
                runner.ClearHistory();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiState> m_UiState;
        std::weak_ptr<SimulationRunner> m_Runner;
        HistoryView m_View;
        bool m_ScrollToCurrent = false;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(UndoHistoryWindowLayer)
