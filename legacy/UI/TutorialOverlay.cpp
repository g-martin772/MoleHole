#include "TutorialOverlay.h"
#include "IconsFontAwesome6.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "spdlog/spdlog.h"

namespace TutorialOverlay {

struct TutorialStep {
    const char* windowName;
    const char* title;
    const char* description;
};

static const TutorialStep s_steps[] = {
    {
        nullptr,
        "Welcome to MoleHole",
        "This tutorial will guide you through the most important features of the\n"
        "black hole simulation. Click Next to continue, or Skip to dismiss."
    },
    {
        "##Sidebar",
        "Sidebar",
        "The sidebar gives you quick access to all panels.\n"
        "Click an icon to toggle the corresponding window on or off."
    },
    {
        "Scene",
        "Scene Panel",
        "The Scene panel lists all objects in your simulation.\n"
        "You can select, rename, and delete objects here.\n"
        "Each object type (Black Hole, Mesh, Sphere) has its own properties."
    },
    {
        "Camera",
        "Camera Controls",
        "Adjust the camera position, orientation, and movement speed.\n"
        "Use W/A/S/D to move and right-click + drag to look around.\n"
        "You can also attach the camera to follow a scene object."
    },
    {
        "System",
        "System & Renderer",
        "Configure rendering quality, ray marching parameters, and\n"
        "visual effects like bloom, accretion disks, and Doppler beaming.\n"
        "Performance and FPS information is also displayed here."
    },
    {
        "Simulation",
        "Simulation",
        "Control the physics simulation: Start, Pause, and Stop.\n"
        "Adjust the time step and simulation speed.\n"
        "Scene objects with physics enabled will interact with each other."
    },
    {
        "Debug",
        "Debug Panel",
        "The Debug panel shows internal state information and lets you\n"
        "toggle debug visualizations such as gravity grids and object paths."
    },
    {
        "Animation Graph",
        "Animation Graph",
        "Create visual scripts using the node-based animation graph.\n"
        "Connect events, math operations, and object properties to\n"
        "build complex behaviors without writing code."
    },
    {
        nullptr,
        "You're Ready!",
        "That covers the essentials. You can re-open this tutorial\n"
        "any time from the Help menu.\n\n"
        "Enjoy exploring black holes!"
    }
};

static constexpr int TOTAL_STEPS = sizeof(s_steps) / sizeof(s_steps[0]);
static int s_currentStep = 0;
static bool s_active = false;
static float s_fadeAlpha = 0.0f;

static ImVec4 OrangeAccent() {
    return ImVec4(180.0f / 255.0f, 100.0f / 255.0f, 40.0f / 255.0f, 1.0f);
}

static ImVec4 OrangeHover() {
    return ImVec4(200.0f / 255.0f, 120.0f / 255.0f, 50.0f / 255.0f, 1.0f);
}

static ImVec4 OrangeActive() {
    return ImVec4(160.0f / 255.0f, 90.0f / 255.0f, 35.0f / 255.0f, 1.0f);
}

static void EnsureWindowVisible(UI* ui, const char* windowName) {
    if (!windowName) return;

    if (strcmp(windowName, "Camera") == 0) *ui->GetShowCameraWindowPtr() = true;
    else if (strcmp(windowName, "System") == 0) *ui->GetShowSystemWindowPtr() = true;
    else if (strcmp(windowName, "Scene") == 0) *ui->GetShowSceneWindowPtr() = true;
    else if (strcmp(windowName, "Debug") == 0) *ui->GetShowDebugWindowPtr() = true;
    else if (strcmp(windowName, "Simulation") == 0) *ui->GetShowSimulationWindowPtr() = true;
    else if (strcmp(windowName, "Animation Graph") == 0) *ui->GetShowAnimationGraphPtr() = true;
    else if (strcmp(windowName, "Viewport - 3D Simulation") == 0) *ui->GetShowSimulationWindowPtr() = true;
}

static ImRect GetWindowRect(const char* windowName) {
    ImGuiWindow* window = ImGui::FindWindowByName(windowName);
    if (window && !window->Hidden) {
        return ImRect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y));
    }
    return ImRect(0, 0, 0, 0);
}

bool IsActive() {
    return s_active;
}

int GetCurrentStep() {
    return s_currentStep;
}

int GetTotalSteps() {
    return TOTAL_STEPS;
}

void Start(UI* ui) {
    s_active = true;
    s_currentStep = 0;
    s_fadeAlpha = 0.0f;
    spdlog::info("Tutorial started");
}

void Stop(UI* ui) {
    s_active = false;
    s_currentStep = 0;
    Application::Params().Set(Params::AppTutorialCompleted, true);
    ui->MarkConfigDirty();
    spdlog::info("Tutorial completed");
}

void Render(UI* ui) {
    if (!s_active) return;

    s_fadeAlpha += ImGui::GetIO().DeltaTime * 4.0f;
    if (s_fadeAlpha > 1.0f) s_fadeAlpha = 1.0f;

    const TutorialStep& step = s_steps[s_currentStep];

    // --- Fullscreen dim overlay as an ImGui window (blocks input to windows behind it) ---
    ImVec2 vpPos = ImGui::GetMainViewport()->Pos;
    ImVec2 vpSize = ImGui::GetMainViewport()->Size;

    ImGui::SetNextWindowPos(vpPos);
    ImGui::SetNextWindowSize(vpSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, s_fadeAlpha * 0.7f));

    ImGuiWindowFlags dimFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoInputs;

    ImGui::Begin("##TutorialDim", nullptr, dimFlags);

    if (step.windowName) {
        ImGuiWindow* window = ImGui::FindWindowByName(step.windowName);
        ImGui::BringWindowToDisplayFront(window);

        EnsureWindowVisible(ui, step.windowName);
        ImRect windowRect = GetWindowRect(step.windowName);
        if (windowRect.GetWidth() > 0 && windowRect.GetHeight() > 0) {
            float pad = 4.0f;
            ImRect padded(windowRect.Min.x - pad, windowRect.Min.y - pad,
                          windowRect.Max.x + pad, windowRect.Max.y + pad);
            ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
            ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(
                ImVec4(180.0f / 255.0f, 100.0f / 255.0f, 40.0f / 255.0f, s_fadeAlpha));
            fgDrawList->AddRect(padded.Min, padded.Max, borderColor, 4.0f, 0, 2.0f);
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    // --- Tutorial Card ---
    ImVec2 screenCenter = ImVec2(vpPos.x + vpSize.x * 0.5f, vpPos.y + vpSize.y * 0.5f);

    float cardWidth = 480.0f;
    float cardMinHeight = 220.0f;
    ImVec2 cardPos;

    if (step.windowName) {
        ImRect windowRect = GetWindowRect(step.windowName);
        if (windowRect.GetWidth() > 0 && windowRect.GetHeight() > 0) {
            float rightSpace = vpSize.x + vpPos.x - windowRect.Max.x;
            float leftSpace = windowRect.Min.x - vpPos.x;

            if (rightSpace > cardWidth + 30.0f) {
                cardPos = ImVec2(windowRect.Max.x + 16.0f,
                                 windowRect.Min.y + (windowRect.GetHeight() - cardMinHeight) * 0.5f);
            } else if (leftSpace > cardWidth + 30.0f) {
                cardPos = ImVec2(windowRect.Min.x - cardWidth - 16.0f,
                                 windowRect.Min.y + (windowRect.GetHeight() - cardMinHeight) * 0.5f);
            } else {
                cardPos = ImVec2(screenCenter.x - cardWidth * 0.5f,
                                 screenCenter.y - cardMinHeight * 0.5f);
            }
        } else {
            cardPos = ImVec2(screenCenter.x - cardWidth * 0.5f,
                             screenCenter.y - cardMinHeight * 0.5f);
        }
    } else {
        cardPos = ImVec2(screenCenter.x - cardWidth * 0.5f,
                         screenCenter.y - cardMinHeight * 0.5f);
    }

    float vpMinY = vpPos.y + 10.0f;
    float vpMaxY = vpPos.y + vpSize.y - cardMinHeight - 10.0f;
    if (cardPos.y < vpMinY) cardPos.y = vpMinY;
    if (cardPos.y > vpMaxY) cardPos.y = vpMaxY;

    ImGui::SetNextWindowPos(cardPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(cardWidth, 0), ImGuiCond_Always);
    ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowBgAlpha(1.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.13f, 0.13f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, OrangeAccent());

    ImGuiWindowFlags cardFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
                                  ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##TutorialCard", nullptr, cardFlags);

    ImFont* iconFont = ui->GetIconFont();
    if (iconFont) {
        ImGui::PushFont(iconFont);
        ImGui::TextColored(OrangeAccent(), "%s", ICON_FA_GRADUATION_CAP);
        ImGui::PopFont();
        ImGui::SameLine();
    }
    ImGui::TextColored(OrangeAccent(), "%s", step.title);

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, OrangeAccent());
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::TextWrapped("%s", step.description);

    ImGui::Spacing();
    ImGui::Spacing();

    char stepLabel[32];
    snprintf(stepLabel, sizeof(stepLabel), "%d / %d", s_currentStep + 1, TOTAL_STEPS);

    float buttonHeight = 32.0f;
    float buttonWidth = 90.0f;

    float totalButtonsWidth = 0.0f;
    if (s_currentStep > 0) totalButtonsWidth += buttonWidth + 8.0f;
    if (s_currentStep < TOTAL_STEPS - 1) {
        totalButtonsWidth += buttonWidth;
    } else {
        totalButtonsWidth += buttonWidth + 10.0f;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, OrangeAccent());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, OrangeHover());
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, OrangeActive());

    float contentWidth = ImGui::GetContentRegionAvail().x;

    ImGui::TextDisabled("%s", stepLabel);
    ImGui::SameLine(contentWidth - totalButtonsWidth);

    if (s_currentStep > 0) {
        if (ImGui::Button(ICON_FA_ARROW_LEFT " Back", ImVec2(buttonWidth, buttonHeight))) {
            s_currentStep--;
            s_fadeAlpha = 0.0f;
        }
        ImGui::SameLine();
    }

    if (s_currentStep < TOTAL_STEPS - 1) {
        if (ImGui::Button("Next " ICON_FA_ARROW_RIGHT, ImVec2(buttonWidth, buttonHeight))) {
            s_currentStep++;
            s_fadeAlpha = 0.0f;
        }
    } else {
        if (ImGui::Button("Finish " ICON_FA_GRADUATION_CAP, ImVec2(buttonWidth + 10.0f, buttonHeight))) {
            Stop(ui);
        }
    }

    ImGui::PopStyleColor(3);

    ImGui::Spacing();
    float skipTextWidth = ImGui::CalcTextSize("Skip Tutorial").x;
    ImGui::SetCursorPosX((contentWidth - skipTextWidth) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.05f));
    if (ImGui::SmallButton("Skip Tutorial")) {
        Stop(ui);
    }
    ImGui::PopStyleColor(3);

    // Draw glow effect on the card's own draw list (renders with the window, not on top of everything)
    ImDrawList* cardDraw = ImGui::GetWindowDrawList();
    ImVec2 cardMin = ImGui::GetWindowPos();
    ImVec2 cardMax = ImVec2(cardMin.x + ImGui::GetWindowSize().x, cardMin.y + ImGui::GetWindowSize().y);
    for (int i = 3; i >= 1; i--) {
        float expand = (float)i * 3.0f;
        ImU32 glowColor = IM_COL32(180, 100, 40, (int)(s_fadeAlpha * 40.0f / (float)i));
        cardDraw->AddRect(
            ImVec2(cardMin.x - expand, cardMin.y - expand),
            ImVec2(cardMax.x + expand, cardMax.y + expand),
            glowColor, 8.0f + expand, 0, 2.0f);
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

}

