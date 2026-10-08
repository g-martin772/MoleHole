#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import std;

using namespace MoleHole;

TEST_CASE("IntroTimeline starts active with no visible letters and zero alpha", "[ui][intro]")
{
    IntroTimeline timeline;
    CHECK(timeline.IsActive());
    CHECK_FALSE(timeline.IsComplete());
    CHECK(timeline.VisibleLetterCount() == 0);
    CHECK(timeline.Alpha() == Catch::Approx(0.0f));
}

TEST_CASE("IntroTimeline reveals one more letter every TextLetterDelay seconds, capped at TotalLetters",
          "[ui][intro]")
{
    IntroTimeline timeline;
    timeline.Update(IntroTimeline::TextLetterDelay * 2.5f);
    CHECK(timeline.VisibleLetterCount() == 2);

    // Advance far past every letter's reveal time -- clamped, not overshooting.
    timeline.Update(IntroTimeline::TotalDuration);
    CHECK(timeline.VisibleLetterCount() == IntroTimeline::TotalLetters);
}

TEST_CASE("IntroTimeline's planet alpha fades in, holds, then fades out over the documented windows",
          "[ui][intro]")
{
    IntroTimeline timeline;

    // Still before PlanetStartDelay: fully transparent.
    timeline.Update(IntroTimeline::PlanetStartDelay * 0.5f);
    CHECK(timeline.Alpha() == Catch::Approx(0.0f));

    // Halfway through the fade-in window.
    IntroTimeline fadeIn;
    fadeIn.Update(IntroTimeline::PlanetStartDelay + IntroTimeline::FadeInDuration * 0.5f);
    CHECK(fadeIn.Alpha() == Catch::Approx(0.5f).margin(0.01f));

    // Inside the hold window: fully opaque.
    IntroTimeline hold;
    hold.Update(IntroTimeline::PlanetStartDelay + IntroTimeline::FadeInDuration + IntroTimeline::HoldDuration * 0.5f);
    CHECK(hold.Alpha() == Catch::Approx(1.0f));

    // Halfway through the fade-out window.
    IntroTimeline fadeOut;
    fadeOut.Update(IntroTimeline::PlanetStartDelay + IntroTimeline::FadeInDuration + IntroTimeline::HoldDuration
                    + IntroTimeline::FadeOutDuration * 0.5f);
    CHECK(fadeOut.Alpha() == Catch::Approx(0.5f).margin(0.01f));
}

TEST_CASE("IntroTimeline becomes complete and inactive exactly once TotalDuration elapses", "[ui][intro]")
{
    IntroTimeline timeline;
    timeline.Update(IntroTimeline::TotalDuration - 0.01f);
    CHECK(timeline.IsActive());
    CHECK_FALSE(timeline.IsComplete());

    timeline.Update(0.02f);
    CHECK_FALSE(timeline.IsActive());
    CHECK(timeline.IsComplete());
    CHECK(timeline.Alpha() == Catch::Approx(0.0f));

    // A further Update is a no-op once complete -- time doesn't keep advancing.
    const float timeAtCompletion = timeline.Time();
    timeline.Update(5.0f);
    CHECK(timeline.Time() == Catch::Approx(timeAtCompletion));
}

TEST_CASE("IntroTimeline::Skip ends the intro immediately regardless of elapsed time", "[ui][intro]")
{
    IntroTimeline timeline;
    timeline.Update(0.05f);
    REQUIRE(timeline.IsActive());

    timeline.Skip();
    CHECK_FALSE(timeline.IsActive());
    CHECK(timeline.IsComplete());
    CHECK(timeline.Alpha() == Catch::Approx(0.0f));

    // Skip is terminal: further Update calls do nothing once skipped.
    timeline.Update(1.0f);
    CHECK_FALSE(timeline.IsActive());
}

TEST_CASE("IntroTimeline's light intensity ramps up via smoothstep only after LightDelay", "[ui][intro]")
{
    IntroTimeline beforeLight;
    beforeLight.Update(IntroTimeline::LightDelay * 0.5f);
    CHECK(beforeLight.LightIntensity() == Catch::Approx(0.0f));

    IntroTimeline afterLight;
    afterLight.Update(IntroTimeline::LightDelay + IntroTimeline::LightDuration);
    CHECK(afterLight.LightIntensity() == Catch::Approx(1.0f));
}
