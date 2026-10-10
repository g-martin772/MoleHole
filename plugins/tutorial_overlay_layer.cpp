#include <imgui.h>
#include <imgui_internal.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    constexpr ImVec4 kAccent(180.0f / 255.0f, 100.0f / 255.0f, 40.0f / 255.0f, 1.0f);
    constexpr ImVec4 kAccentHover(200.0f / 255.0f, 120.0f / 255.0f, 50.0f / 255.0f, 1.0f);
    constexpr ImVec4 kAccentActive(160.0f / 255.0f, 90.0f / 255.0f, 35.0f / 255.0f, 1.0f);

    ImRect WindowRectOf(const char* windowName)
    {
        if (!windowName) return ImRect(0.0f, 0.0f, 0.0f, 0.0f);
        ImGuiWindow* window = ImGui::FindWindowByName(windowName);
        if (window && !window->Hidden)
        {
            return ImRect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y));
        }
        return ImRect(0.0f, 0.0f, 0.0f, 0.0f);
    }

    struct TutorialOverlayLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, UiState, AppStateService>;

        TutorialOverlayLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<UiState> uiState,
                             std::shared_ptr<AppStateService> appState)
            : HotReloadableLayer(logger), m_UiState(std::move(uiState)), m_AppState(std::move(appState))
        {
        }

        void OnUiRender() override
        {
            if (!m_UiState->TutorialActive)
            {
                m_WasActive = false;
                return;
            }

            m_UiState->TutorialStep = std::clamp(m_UiState->TutorialStep, 0, TutorialStepCount() - 1);
            if (!m_WasActive || m_UiState->TutorialStep != m_LastRenderedStep)
            {
                m_FadeAlpha = 0.0f;
                m_LastRenderedStep = m_UiState->TutorialStep;
            }
            m_WasActive = true;
            m_FadeAlpha = AdvanceTutorialFadeAlpha(m_FadeAlpha, ImGui::GetIO().DeltaTime);

            const TutorialStep& step = GetTutorialSteps()[static_cast<std::size_t>(m_UiState->TutorialStep)];

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            RenderDimBackdrop(step, viewport->Pos, viewport->Size);
            RenderCard(step, viewport->Pos, viewport->Size);
        }

    private:
        void RenderDimBackdrop(const TutorialStep& step, ImVec2 vpPos, ImVec2 vpSize)
        {
            ImGui::SetNextWindowPos(vpPos);
            ImGui::SetNextWindowSize(vpSize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, m_FadeAlpha * 0.7f));

            constexpr ImGuiWindowFlags dimFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoInputs;

            ImGui::Begin("##TutorialDim", nullptr, dimFlags);
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

            if (step.WindowName)
            {
                ApplyTutorialStepVisibility(*m_UiState, step);
                if (ImGuiWindow* window = ImGui::FindWindowByName(step.WindowName))
                {
                    ImGui::BringWindowToDisplayFront(window);
                }

                const ImRect rect = WindowRectOf(step.WindowName);
                if (rect.GetWidth() > 0.0f && rect.GetHeight() > 0.0f)
                {
                    constexpr float pad = 4.0f;
                    const ImRect padded(rect.Min.x - pad, rect.Min.y - pad, rect.Max.x + pad, rect.Max.y + pad);
                    const ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(kAccent.x, kAccent.y, kAccent.z, m_FadeAlpha));
                    ImGui::GetForegroundDrawList()->AddRect(padded.Min, padded.Max, borderColor, 4.0f, 0, 2.0f);
                }
            }

            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
        }

        void RenderCard(const TutorialStep& step, ImVec2 vpPos, ImVec2 vpSize)
        {
            constexpr float cardWidth = 480.0f;
            constexpr float cardMinHeight = 220.0f;
            const ImVec2 screenCenter(vpPos.x + vpSize.x * 0.5f, vpPos.y + vpSize.y * 0.5f);

            ImVec2 cardPos(screenCenter.x - cardWidth * 0.5f, screenCenter.y - cardMinHeight * 0.5f);
            if (step.WindowName)
            {
                const ImRect rect = WindowRectOf(step.WindowName);
                if (rect.GetWidth() > 0.0f && rect.GetHeight() > 0.0f)
                {
                    const float rightSpace = vpSize.x + vpPos.x - rect.Max.x;
                    const float leftSpace = rect.Min.x - vpPos.x;
                    if (rightSpace > cardWidth + 30.0f)
                    {
                        cardPos = ImVec2(rect.Max.x + 16.0f, rect.Min.y + (rect.GetHeight() - cardMinHeight) * 0.5f);
                    }
                    else if (leftSpace > cardWidth + 30.0f)
                    {
                        cardPos = ImVec2(rect.Min.x - cardWidth - 16.0f,
                                         rect.Min.y + (rect.GetHeight() - cardMinHeight) * 0.5f);
                    }
                }
            }

            const float vpMinY = vpPos.y + 10.0f;
            const float vpMaxY = vpPos.y + vpSize.y - cardMinHeight - 10.0f;
            cardPos.y = std::clamp(cardPos.y, vpMinY, vpMaxY);

            ImGui::SetNextWindowPos(cardPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(cardWidth, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowFocus();
            ImGui::SetNextWindowBgAlpha(1.0f);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, kAccent);

            constexpr ImGuiWindowFlags cardFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;

            ImGui::Begin("##TutorialCard", nullptr, cardFlags);

            ImGui::TextColored(kAccent, "%s", step.Title);
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Separator, kAccent);
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Spacing();

            ImGui::TextWrapped("%s", step.Description);
            ImGui::Spacing();
            ImGui::Spacing();

            const float contentWidth = ImGui::GetContentRegionAvail().x;
            RenderNavRow(contentWidth);

            ImGui::Spacing();
            const float skipWidth = ImGui::CalcTextSize("Skip Tutorial").x;
            ImGui::SetCursorPosX((contentWidth - skipWidth) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.05f));
            if (ImGui::SmallButton("Skip Tutorial"))
            {
                StopTutorial();
            }
            ImGui::PopStyleColor(3);

            DrawCardGlow();

            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
        }

        void RenderNavRow(float contentWidth)
        {
            const std::string stepLabel =
                std::format("{} / {}", m_UiState->TutorialStep + 1, TutorialStepCount());

            constexpr float buttonWidth = 90.0f;
            constexpr float buttonHeight = 32.0f;
            const bool isFirst = IsFirstTutorialStep(m_UiState->TutorialStep);
            const bool isLast = IsLastTutorialStep(m_UiState->TutorialStep);

            float totalButtonsWidth = isLast ? buttonWidth + 10.0f : buttonWidth;
            if (!isFirst) totalButtonsWidth += buttonWidth + 8.0f;

            ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);

            ImGui::TextDisabled("%s", stepLabel.c_str());
            ImGui::SameLine(contentWidth - totalButtonsWidth);

            if (!isFirst)
            {
                if (ImGui::Button("Back", ImVec2(buttonWidth, buttonHeight)))
                {
                    m_UiState->TutorialStep = PreviousTutorialStep(m_UiState->TutorialStep);
                }
                ImGui::SameLine();
            }

            if (!isLast)
            {
                if (ImGui::Button("Next", ImVec2(buttonWidth, buttonHeight)))
                {
                    m_UiState->TutorialStep = NextTutorialStep(m_UiState->TutorialStep);
                }
            }
            else
            {
                if (ImGui::Button("Finish", ImVec2(buttonWidth + 10.0f, buttonHeight)))
                {
                    StopTutorial();
                }
            }

            ImGui::PopStyleColor(3);
        }

        void DrawCardGlow() const
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 cardMin = ImGui::GetWindowPos();
            const ImVec2 cardMax(cardMin.x + ImGui::GetWindowSize().x, cardMin.y + ImGui::GetWindowSize().y);
            for (int i = 3; i >= 1; --i)
            {
                const float expand = static_cast<float>(i) * 3.0f;
                const ImU32 glowColor =
                    IM_COL32(180, 100, 40, static_cast<int>(m_FadeAlpha * 40.0f / static_cast<float>(i)));
                drawList->AddRect(ImVec2(cardMin.x - expand, cardMin.y - expand),
                                  ImVec2(cardMax.x + expand, cardMax.y + expand), glowColor, 8.0f + expand, 0, 2.0f);
            }
        }

        void StopTutorial()
        {
            m_UiState->TutorialActive = false;
            m_AppState->SetTutorialCompleted(true);
            m_Logger->Info("Tutorial completed");
        }

        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<AppStateService> m_AppState;
        float m_FadeAlpha = 0.0f;
        int m_LastRenderedStep = -1;
        bool m_WasActive = false;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(TutorialOverlayLayer)
