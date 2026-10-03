import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct TopBarLayer final : public HotReloadableLayer
    {
        using Dependencies =
            std::tuple<Logger, FileDialog, SceneManager, UiPreferences, FontAssetCatalog, UiState>;

        TopBarLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<FileDialog> fileDialog,
                   std::shared_ptr<SceneManager> scenes, std::shared_ptr<UiPreferences> uiPreferences,
                   std::shared_ptr<FontAssetCatalog> fontAssets, std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_FileDialog(std::move(fileDialog)), m_Scenes(std::move(scenes)),
              m_UiPreferences(std::move(uiPreferences)), m_FontAssets(std::move(fontAssets)),
              m_UiState(std::move(uiState))
        {
        }

        void OnUiRender() override
        {
            RenderMenuBar();
            RenderSettingsPopup();
        }

    private:
        void RenderMenuBar()
        {
            if (!ImGui::BeginMainMenuBar()) return;

            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Scene"))
                {
                    m_UiState->PendingNewScene = true;
                }
                if (ImGui::MenuItem("Open Scene..."))
                {
                    if (const auto path = m_FileDialog->OpenFile({FileDialogFilter{"Scene", "yaml"}}))
                    {
                        m_UiState->PendingLoadScenePath = path->string();
                    }
                }
                const bool canSave = !m_UiState->CurrentScenePath.empty();
                if (ImGui::MenuItem("Save Scene", nullptr, false, canSave))
                {
                    try
                    {
                        m_Scenes->SaveSceneToFile(m_UiState->CurrentSceneName, m_UiState->CurrentScenePath);
                        m_Logger->Info("Scene saved to '{}'", m_UiState->CurrentScenePath);
                    }
                    catch (const std::exception& error)
                    {
                        m_Logger->Error("Failed to save scene: {}", error.what());
                    }
                }
                if (ImGui::MenuItem("Save Scene As..."))
                {
                    const auto defaultName = m_UiState->CurrentSceneName.empty()
                                                 ? std::string("scene.yaml")
                                                 : m_UiState->CurrentSceneName + ".yaml";
                    if (const auto path =
                            m_FileDialog->SaveFile({FileDialogFilter{"Scene", "yaml"}}, defaultName))
                    {
                        try
                        {
                            m_Scenes->SaveSceneToFile(m_UiState->CurrentSceneName, *path);
                            m_UiState->CurrentScenePath = path->string();
                            m_Logger->Info("Scene saved to '{}'", m_UiState->CurrentScenePath);
                        }
                        catch (const std::exception& error)
                        {
                            m_Logger->Error("Failed to save scene: {}", error.what());
                        }
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Camera", nullptr, &m_UiState->ShowCameraWindow);
                ImGui::MenuItem("System", nullptr, &m_UiState->ShowSystemWindow);
                ImGui::MenuItem("Scene", nullptr, &m_UiState->ShowSceneWindow);
                ImGui::MenuItem("Debug", nullptr, &m_UiState->ShowDebugWindow);
                ImGui::MenuItem("Viewport HUD", nullptr, &m_UiState->ShowViewportHud);
                ImGui::MenuItem("General Relativity", nullptr, &m_UiState->ShowGeneralRelativityWindow);
                ImGui::MenuItem("Science", nullptr, &m_UiState->ShowScienceWindow);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("About"))
                {
                    m_UiState->ShowSettingsWindow = true;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        void RenderSettingsPopup()
        {
            if (m_UiState->ShowSettingsWindow && !ImGui::IsPopupOpen("Settings"))
            {
                ImGui::OpenPopup("Settings");
            }

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(480, 360), ImGuiCond_Appearing);

            if (ImGui::BeginPopupModal("Settings", &m_UiState->ShowSettingsWindow, ImGuiWindowFlags_NoResize))
            {
                SectionHeader("MOLEHOLE");
                ImGui::TextWrapped(
                    "A relativistic black hole visualizer -- gravitational lensing, accretion disks, "
                    "and spacetime curvature rendered in real time.");

                SectionHeader("INTERFACE");
                float scale = m_UiPreferences->GetUiScale();
                if (ImGui::SliderFloat("UI Scale", &scale, 0.5f, 2.0f, "%.2f"))
                {
                    m_UiPreferences->SetUiScale(scale);
                }

                const auto currentFont = m_UiPreferences->GetFontName();
                const auto currentSize = m_UiPreferences->GetFontSize();
                const auto availableFonts = m_FontAssets->GetAvailableFonts();
                if (ImGui::BeginCombo("Font", currentFont.c_str()))
                {
                    for (const auto& font : availableFonts)
                    {
                        const bool isSelected = font.Name == currentFont;
                        if (ImGui::Selectable(font.Name.c_str(), isSelected))
                        {
                            m_UiPreferences->SetFont(font.Name, currentSize);
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                float fontSize = currentSize;
                if (ImGui::SliderFloat("Font Size", &fontSize, 10.0f, 32.0f, "%.0f"))
                {
                    m_UiPreferences->SetFont(currentFont, fontSize);
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                if (ImGui::Button("Close", ImVec2(120, 0)))
                {
                    m_UiState->ShowSettingsWindow = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        std::shared_ptr<FileDialog> m_FileDialog;
        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiPreferences> m_UiPreferences;
        std::shared_ptr<FontAssetCatalog> m_FontAssets;
        std::shared_ptr<UiState> m_UiState;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(TopBarLayer)
