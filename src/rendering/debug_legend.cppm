export module MoleHole:Rendering.DebugLegend;

import std;

export namespace MoleHole
{
    struct LegendColor
    {
        float R = 0.0f;
        float G = 0.0f;
        float B = 0.0f;

        friend constexpr bool operator==(const LegendColor& a, const LegendColor& b) noexcept
        {
            return a.R == b.R && a.G == b.G && a.B == b.B;
        }
    };

    struct LegendEntry
    {
        std::string_view Id;
        std::string_view Label;
        std::string_view Meaning;
        LegendColor Color;
    };

    struct OverlayLegend
    {
        std::string_view SettingKey;
        std::span<const LegendEntry> Entries;
    };

    [[nodiscard]] constexpr LegendColor DebugHeat(float t) noexcept
    {
        t = std::clamp(t, 0.0f, 1.0f);
        const auto unit = [](const float v) { return std::clamp(v, 0.0f, 1.0f); };
        const float middle = 3.0f * t - 1.5f;
        return {unit(1.5f * t - 0.25f), unit(1.5f - (middle < 0.0f ? -middle : middle)), unit(1.25f - 1.5f * t)};
    }

    inline constexpr std::string_view kStepCountLowLabel = "few steps";
    inline constexpr std::string_view kStepCountHighLabel = "step budget";
    inline constexpr std::string_view kStepCountNote = "Colour = sqrt(steps / step budget), so cheap and expensive pixels both stay readable.";

    inline constexpr std::array<LegendEntry, 6> kTerminationLegend{{
        {"TERM_SKY", "Sky", "ray escaped to the background", {0.04f, 0.08f, 0.22f}},
        {"TERM_HORIZON", "Horizon", "ray fell into a black hole", {0.9f, 0.05f, 0.05f}},
        {"TERM_OBJECT", "Object", "ray hit a sphere", {0.1f, 0.8f, 0.2f}},
        {"TERM_DISK", "Opaque disk", "the accretion disk absorbed the ray", {1.0f, 0.6f, 0.0f}},
        {"TERM_STEPS", "Out of steps", "step budget ran out before the ray ended", {1.0f, 1.0f, 1.0f}},
        {"TERM_CROSSHAIR", "Crosshair", "third-person camera crosshair", {0.0f, 0.9f, 0.9f}},
    }};

    inline constexpr std::array<LegendEntry, 1> kGravityGridLegend{{
        {"kGravityGridColor", "Grid lines", "plane at y = -5 sagging towards every black hole, sphere and mesh", {0.1f, 0.1f, 0.8f}},
    }};

    inline constexpr std::array<LegendEntry, 2> kObjectPathLegend{{
        {"kMeshColor", "Meshes", "recent world-space path of each mesh while playing", {0.2f, 0.8f, 0.2f}},
        {"kSphereColor", "Spheres", "recent world-space path of each sphere while playing", {0.8f, 0.2f, 0.8f}},
    }};

    inline constexpr std::array<LegendEntry, 6> kColliderLegend{{
        {"eARGB_MAGENTA", "Collision shapes", "outline of every static and dynamic collider", {1.0f, 0.0f, 1.0f}},
        {"eARGB_YELLOW", "Bounding boxes", "axis-aligned bounds of every collider", {1.0f, 1.0f, 0.0f}},
        {"eARGB_RED", "X axis", "of actor and body frames; also contact points", {1.0f, 0.0f, 0.0f}},
        {"eARGB_GREEN", "Y axis", "of actor and body frames", {0.0f, 1.0f, 0.0f}},
        {"eARGB_BLUE", "Z axis", "of actor and body frames", {0.0f, 0.0f, 1.0f}},
        {"eARGB_WHITE", "Inertia box", "body inertia box and velocity arrow; the box turns red when the body sleeps", {1.0f, 1.0f, 1.0f}},
    }};

    inline constexpr std::array<OverlayLegend, 3> kOverlayLegends{{
        {"overlay.gravity_grid", kGravityGridLegend},
        {"overlay.physics_colliders", kColliderLegend},
        {"overlay.object_paths", kObjectPathLegend},
    }};
}
