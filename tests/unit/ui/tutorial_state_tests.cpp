#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import std;

using namespace MoleHole;

TEST_CASE("GetTutorialSteps returns a non-empty step list whose first and last steps have no "
          "target window", "[ui][tutorial]")
{
    const auto steps = GetTutorialSteps();
    REQUIRE(steps.size() > 1);
    CHECK(steps.front().WindowName == nullptr);
    CHECK(steps.back().WindowName == nullptr);
    CHECK(TutorialStepCount() == static_cast<int>(steps.size()));
}

TEST_CASE("IsFirstTutorialStep/IsLastTutorialStep correctly bound the step range", "[ui][tutorial]")
{
    CHECK(IsFirstTutorialStep(0));
    CHECK_FALSE(IsLastTutorialStep(0));

    const int lastIndex = TutorialStepCount() - 1;
    CHECK(IsLastTutorialStep(lastIndex));
    CHECK_FALSE(IsFirstTutorialStep(lastIndex));

    const int middleIndex = lastIndex / 2;
    REQUIRE(middleIndex > 0);
    REQUIRE(middleIndex < lastIndex);
    CHECK_FALSE(IsFirstTutorialStep(middleIndex));
    CHECK_FALSE(IsLastTutorialStep(middleIndex));
}

TEST_CASE("NextTutorialStep/PreviousTutorialStep clamp at the ends, matching the Back/Next "
          "button gating", "[ui][tutorial]")
{
    CHECK(PreviousTutorialStep(0) == 0);
    CHECK(NextTutorialStep(0) == 1);

    const int lastIndex = TutorialStepCount() - 1;
    CHECK(NextTutorialStep(lastIndex) == lastIndex);
    CHECK(PreviousTutorialStep(lastIndex) == lastIndex - 1);
}

TEST_CASE("AdvanceTutorialFadeAlpha accumulates at the documented rate and clamps to 1",
          "[ui][tutorial]")
{
    float alpha = 0.0f;
    alpha = AdvanceTutorialFadeAlpha(alpha, 0.1f);
    CHECK(alpha == Catch::Approx(0.4f));

    alpha = AdvanceTutorialFadeAlpha(alpha, 0.1f);
    CHECK(alpha == Catch::Approx(0.8f));

    // A third step would overshoot 1.0 (0.8 + 0.4 = 1.2) without clamping.
    alpha = AdvanceTutorialFadeAlpha(alpha, 0.1f);
    CHECK(alpha == Catch::Approx(1.0f));

    // Further advancing a fully-faded-in overlay stays clamped.
    alpha = AdvanceTutorialFadeAlpha(alpha, 1.0f);
    CHECK(alpha == Catch::Approx(1.0f));
}

TEST_CASE("ApplyTutorialStepVisibility forces exactly the target window's flag and leaves "
          "the rest untouched", "[ui][tutorial]")
{
    UiState uiState;
    const bool sceneWindowBefore = uiState.ShowSceneWindow; // defaults true, unrelated to this step
    const TutorialStep debugStep{"Debug", "Debug Panel", "..."};
    ApplyTutorialStepVisibility(uiState, debugStep);

    CHECK(uiState.ShowDebugWindow);
    CHECK_FALSE(uiState.ShowCameraWindow);
    CHECK_FALSE(uiState.ShowSystemWindow);
    CHECK(uiState.ShowSceneWindow == sceneWindowBefore);
    CHECK_FALSE(uiState.ShowGeneralRelativityWindow);
    CHECK_FALSE(uiState.ShowScienceWindow);
    CHECK_FALSE(uiState.ShowAnimationGraphWindow);
}

TEST_CASE("ApplyTutorialStepVisibility is a no-op for a step with no target window",
          "[ui][tutorial]")
{
    UiState uiState;
    const bool sceneWindowBefore = uiState.ShowSceneWindow; // defaults true, must stay untouched too
    const TutorialStep introStep{nullptr, "Welcome", "..."};
    ApplyTutorialStepVisibility(uiState, introStep);

    CHECK_FALSE(uiState.ShowCameraWindow);
    CHECK_FALSE(uiState.ShowSystemWindow);
    CHECK(uiState.ShowSceneWindow == sceneWindowBefore);
    CHECK_FALSE(uiState.ShowDebugWindow);
    CHECK_FALSE(uiState.ShowGeneralRelativityWindow);
    CHECK_FALSE(uiState.ShowScienceWindow);
    CHECK_FALSE(uiState.ShowAnimationGraphWindow);
}

TEST_CASE("StartTutorial resets to step 0 and marks the overlay active even after a previous "
          "partial run", "[ui][tutorial]")
{
    UiState uiState;
    uiState.TutorialStep = 5;
    uiState.TutorialActive = false;

    StartTutorial(uiState);

    CHECK(uiState.TutorialActive);
    CHECK(uiState.TutorialStep == 0);
}
