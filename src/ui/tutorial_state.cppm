export module MoleHole:UI.TutorialState;

import std;
import :UI.State;

export namespace MoleHole
{
    struct TutorialStep
    {
        const char* WindowName;
        const char* Title;
        const char* Description;
    };

    inline constexpr TutorialStep kTutorialSteps[] = {
        {
            nullptr,
            "Welcome to MoleHole",
            "This tutorial will walk you through the main panels of the black hole simulator.\n"
            "Click Next to continue, or Skip Tutorial to dismiss it."
        },
        {
            "##Sidebar",
            "Sidebar",
            "The sidebar gives you quick access to every panel.\n"
            "Click an icon to toggle the corresponding window on or off."
        },
        {
            "Scene",
            "Scene Panel",
            "Lists every object in the simulation. Select one to edit its properties\n"
            "or delete it, and add new Black Holes, Spheres, or Meshes from here."
        },
        {
            "Camera",
            "Camera Controls",
            "Adjust movement speed and mouse sensitivity, and keep an eye on the\n"
            "camera's live position and field of view.\n"
            "Use W/A/S/D to move, Q/E for down/up, and right-click + drag to look around."
        },
        {
            "System",
            "System & Performance",
            "Shows live FPS and frame-time history, along with your GPU, driver,\n"
            "and Vulkan API version, and the viewport's current render resolution."
        },
        {
            "Debug",
            "Debug Panel",
            "Toggle individual rendering effects (lensing, redshift, Doppler beaming,\n"
            "the accretion disk) and debug visualizations like the gravity grid,\n"
            "physics collider wireframe, and object path trails."
        },
        {
            nullptr,
            "Playback Controls",
            "The floating Play/Pause and Stop buttons above the viewport control the\n"
            "physics simulation. Stop restores the scene to how it looked the moment\n"
            "you pressed Play."
        },
        {
            "General Relativity",
            "General Relativity",
            "Pick which exact solution to Einstein's field equations the ray tracer\n"
            "uses -- Schwarzschild, Kerr, Reissner-Nordstrom, or Kerr-Newman -- and\n"
            "see its metric tensor."
        },
        {
            "Science",
            "Science",
            "A browsable mini-encyclopedia covering general relativity, black holes,\n"
            "the Schwarzschild and Kerr metrics, accretion disks, and the math behind\n"
            "the simulation."
        },
        {
            "Animation Graph",
            "Animation Graph",
            "Build visual scripts with a node-based editor: connect events, math\n"
            "operations, and object properties to animate the scene without writing\n"
            "code."
        },
        {
            nullptr,
            "You're Ready!",
            "That covers the essentials. You can re-open this tutorial any time from\n"
            "the Help menu.\n\nEnjoy exploring black holes!"
        }
    };

    [[nodiscard]] inline std::span<const TutorialStep> GetTutorialSteps() noexcept
    {
        return kTutorialSteps;
    }

    [[nodiscard]] inline int TutorialStepCount() noexcept
    {
        return static_cast<int>(GetTutorialSteps().size());
    }

    [[nodiscard]] inline bool IsFirstTutorialStep(int step) noexcept { return step <= 0; }

    [[nodiscard]] inline bool IsLastTutorialStep(int step) noexcept
    {
        return step >= TutorialStepCount() - 1;
    }

    [[nodiscard]] inline int NextTutorialStep(int step) noexcept
    {
        return std::min(step + 1, TutorialStepCount() - 1);
    }

    [[nodiscard]] inline int PreviousTutorialStep(int step) noexcept
    {
        return std::max(step - 1, 0);
    }

    [[nodiscard]] inline float AdvanceTutorialFadeAlpha(float alpha, float deltaTime) noexcept
    {
        return std::min(alpha + deltaTime * 4.0f, 1.0f);
    }

    inline void StartTutorial(UiState& uiState) noexcept
    {
        uiState.TutorialActive = true;
        uiState.TutorialStep = 0;
    }

    inline void ApplyTutorialStepVisibility(UiState& uiState, const TutorialStep& step) noexcept
    {
        if (!step.WindowName) return;
        const std::string_view windowName(step.WindowName);

        if (windowName == "Camera") uiState.ShowCameraWindow = true;
        else if (windowName == "System") uiState.ShowSystemWindow = true;
        else if (windowName == "Scene") uiState.ShowSceneWindow = true;
        else if (windowName == "Debug") uiState.ShowDebugWindow = true;
        else if (windowName == "General Relativity") uiState.ShowGeneralRelativityWindow = true;
        else if (windowName == "Science") uiState.ShowScienceWindow = true;
        else if (windowName == "Animation Graph") uiState.ShowAnimationGraphWindow = true;
    }
}
