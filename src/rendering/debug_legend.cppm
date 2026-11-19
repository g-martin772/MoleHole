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

    [[nodiscard]] constexpr LegendColor OverBackground(const LegendColor& color, const float opacity,
                                                       const LegendColor& background) noexcept
    {
        const auto mix = [&](const float c, const float b) { return c * opacity + b * (1.0f - opacity); };
        return {mix(color.R, background.R), mix(color.G, background.G), mix(color.B, background.B)};
    }

    inline constexpr float kGravityGridOpacity = 0.7f;
    inline constexpr LegendColor kGravityGridColor{0.1f, 0.1f, 0.8f};
    inline constexpr LegendColor kViewportBackground{0.0f, 0.0f, 0.0f};

    struct DebugLineRemap
    {
        std::uint32_t Rgb;
        LegendColor Color;
    };

    // PhysX draws the angular velocity arrow in 0x000000, invisible on the dark viewport
    inline constexpr std::array<DebugLineRemap, 1> kPhysicsDebugRemap{{
        {0x000000u, {1.0f, 0.55f, 0.0f}},
    }};

    [[nodiscard]] constexpr LegendColor PhysicsDebugColor(const std::uint32_t argb) noexcept
    {
        const std::uint32_t rgb = argb & 0xFFFFFFu;
        for (const auto& remap : kPhysicsDebugRemap)
        {
            if (remap.Rgb == rgb) return remap.Color;
        }
        return {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                static_cast<float>(rgb & 0xFF) / 255.0f};
    }

    inline constexpr std::string_view kStepCountLowLabel = "few steps";
    inline constexpr std::string_view kStepCountHighLabel = "step budget";
    inline constexpr std::string_view kStepCountNote = "Colour = sqrt(steps / step budget), so cheap and expensive pixels both stay readable.";

    inline constexpr std::string_view kConstraintDriftLowLabel = "1e-6";
    inline constexpr std::string_view kConstraintDriftHighLabel = "1";
    inline constexpr std::string_view kConstraintDriftNote = "Colour = log10 of the largest |2H| / E^2 along the ray. Light paths have H = 0; "
                                                             "drift near the horizon is expected and harmless.";

    // ERGOSPHERE_TINT / ISCO_RING_COLOR in shaders/debug_view.glsl
    inline constexpr std::array<LegendEntry, 2> kMetricStructureLegend{{
        {"ERGOSPHERE_TINT", "Ergosphere", "inside it nothing can stay at rest; tint grows with the path length inside", {0.6f, 0.0f, 0.8f}},
        {"ISCO_RING_COLOR", "ISCO", "innermost stable circular orbit, where rays cross the equatorial plane", {0.0f, 1.0f, 0.0f}},
    }};

    // IMAGE_ORDER_* in shaders/debug_view.glsl
    inline constexpr std::array<LegendEntry, 5> kImageOrderLegend{{
        {"IMAGE_ORDER_0", "0", "no disk; the ray never crossed the equatorial plane near the hole", {0.06f, 0.06f, 0.14f}},
        {"IMAGE_ORDER_1", "1", "direct image: the disk as you would see it without the hole", {0.95f, 0.55f, 0.10f}},
        {"IMAGE_ORDER_2", "2", "secondary image: the far side of the disk bent under or over the hole", {0.20f, 0.65f, 1.00f}},
        {"IMAGE_ORDER_3", "3", "first photon ring: light that went once around the hole", {0.35f, 0.95f, 0.35f}},
        {"IMAGE_ORDER_4", "4 or more", "higher photon rings, exponentially thinner", {1.00f, 1.00f, 1.00f}},
    }};
    inline constexpr float kImageOrderSkyDimming = 0.4f;
    inline constexpr float kImageOrderCapturedDimming = 0.2f;
    inline constexpr std::string_view kImageOrderNote = "Bright: the disk, by the order of the image it shows (equator crossings up to it). "
                                                        "Dim: everything else, by how often its ray crossed the plane; darkest where the ray "
                                                        "fell in. Counted within 50 M of the first hole.";

    // SKY_QUADRANT_* / SKY_LINE_COLOR in shaders/debug_view.glsl
    inline constexpr std::array<LegendEntry, 5> kSkyCheckerboardLegend{{
        {"SKY_QUADRANT_0", "Longitude -180..-90", "longitude about the spin axis, from the hole frame's x axis", {0.48f, 0.13f, 0.10f}},
        {"SKY_QUADRANT_1", "Longitude -90..0", "next quadrant", {0.11f, 0.40f, 0.14f}},
        {"SKY_QUADRANT_2", "Longitude 0..90", "next quadrant", {0.13f, 0.21f, 0.53f}},
        {"SKY_QUADRANT_3", "Longitude 90..180", "last quadrant", {0.50f, 0.42f, 0.08f}},
        {"SKY_LINE_COLOR", "Lines", "celestial equator (the disk plane) and the four quadrant meridians", {0.55f, 0.55f, 0.55f}},
    }};
    inline constexpr std::string_view kSkyCheckerboardNote = "10 degree cells alternate in brightness; the southern hemisphere is darker.";

    // REDSHIFT_* in shaders/debug_view.glsl: debugDiverging(log2 g / REDSHIFT_LOG2_RANGE)
    inline constexpr LegendColor kRedshiftRed{0.85f, 0.12f, 0.08f};
    inline constexpr LegendColor kRedshiftNeutral{0.95f, 0.95f, 0.95f};
    inline constexpr LegendColor kRedshiftBlue{0.10f, 0.35f, 0.95f};
    inline constexpr float kRedshiftLog2Range = 1.0f;

    [[nodiscard]] constexpr LegendColor DebugDiverging(float t) noexcept
    {
        t = std::clamp(t, -1.0f, 1.0f);
        const auto mix = [](const LegendColor& a, const LegendColor& b, const float f)
        {
            return LegendColor{a.R * (1.0f - f) + b.R * f, a.G * (1.0f - f) + b.G * f, a.B * (1.0f - f) + b.B * f};
        };
        return t < 0.0f ? mix(kRedshiftNeutral, kRedshiftRed, -t) : mix(kRedshiftNeutral, kRedshiftBlue, t);
    }

    inline constexpr std::string_view kRedshiftLowLabel = "g = 0.5 (redder)";
    inline constexpr std::string_view kRedshiftMiddleLabel = "g = 1";
    inline constexpr std::string_view kRedshiftHighLabel = "g = 2 (bluer)";
    inline constexpr std::string_view kRedshiftNote = "g = observed / emitted frequency of the disk light, weighted by how much each disk "
                                                      "crossing contributes. The whole shift, Doppler and gravity together. Pixels without disk "
                                                      "are grey. Lensed and Integrated transports.";
    inline constexpr std::string_view kGravitationalRedshiftNote = "Only the gravitational part: the ratio of the clock rates (ZAMO lapses) "
                                                                   "of the gas and the camera. Always red, deepest at the inner edge.";
    inline constexpr std::string_view kDopplerShiftNote = "Only the motion part, g divided by its gravitational part: blue where the gas "
                                                          "comes towards you, red where it recedes.";

    inline constexpr std::array<LegendEntry, 6> kTerminationLegend{{
        {"TERM_SKY", "Sky", "ray escaped to the background", {0.04f, 0.08f, 0.22f}},
        {"TERM_HORIZON", "Horizon", "ray fell into a black hole", {0.9f, 0.05f, 0.05f}},
        {"TERM_OBJECT", "Object", "ray hit a sphere", {0.1f, 0.8f, 0.2f}},
        {"TERM_DISK", "Opaque disk", "the accretion disk absorbed the ray", {1.0f, 0.6f, 0.0f}},
        {"TERM_STEPS", "Out of steps", "step budget ran out before the ray ended", {1.0f, 1.0f, 1.0f}},
        {"TERM_CROSSHAIR", "Crosshair", "third-person camera crosshair", {0.0f, 0.9f, 0.9f}},
    }};

    inline constexpr std::array<LegendEntry, 1> kGravityGridLegend{{
        {"kGravityGridColor", "Grid lines", "plane at y = -5 sagging towards every black hole, sphere and mesh",
         OverBackground(kGravityGridColor, kGravityGridOpacity, kViewportBackground)},
    }};

    inline constexpr std::array<LegendEntry, 2> kObjectPathLegend{{
        {"kMeshColor", "Meshes", "recent world-space path of each mesh while playing", {0.2f, 0.8f, 0.2f}},
        {"kSphereColor", "Spheres", "recent world-space path of each sphere while playing", {0.8f, 0.2f, 0.8f}},
    }};

    inline constexpr std::array<LegendEntry, 8> kColliderLegend{{
        {"eARGB_MAGENTA", "Collision shapes", "outline of every static and dynamic collider", {1.0f, 0.0f, 1.0f}},
        {"eARGB_YELLOW", "Bounding boxes", "axis-aligned bounds of every collider", {1.0f, 1.0f, 0.0f}},
        {"eARGB_RED", "X axis", "of actor and body frames; also contact points", {1.0f, 0.0f, 0.0f}},
        {"eARGB_GREEN", "Y axis", "of actor and body frames", {0.0f, 1.0f, 0.0f}},
        {"eARGB_BLUE", "Z axis", "of actor and body frames", {0.0f, 0.0f, 1.0f}},
        {"eARGB_WHITE", "Inertia box", "body inertia box; fades while the body settles and turns red when it sleeps", {1.0f, 1.0f, 1.0f}},
        {"PX_LIN_VELOCITY", "Linear velocity", "arrow from the body centre along its velocity", PhysicsDebugColor(0xFFFFFFu)},
        {"PX_ANG_VELOCITY", "Angular velocity", "arrow along the axis of rotation", PhysicsDebugColor(0x000000u)},
    }};

    // Colours of src/rendering/orbit_overlays.cppm; retrograde rings are drawn at half brightness
    inline constexpr std::array<LegendEntry, 5> kCharacteristicRadiiLegend{{
        {"kHorizonOverlayColor", "Event horizon", "outer horizon r+ (equator and two meridians)", {0.85f, 0.85f, 0.85f}},
        {"kErgosphereOverlayColor", "Ergosphere", "static limit, in two meridian planes", {0.65f, 0.35f, 1.0f}},
        {"kPhotonOrbitOverlayColor", "Photon orbit", "circular light orbit; dim ring: retrograde", {1.0f, 0.6f, 0.15f}},
        {"kMarginallyBoundOverlayColor", "Marginally bound", "circular orbit with E = 1; dim ring: retrograde", {0.35f, 0.9f, 0.4f}},
        {"kIscoOverlayColor", "ISCO", "innermost stable circular orbit; dim ring: retrograde", {0.2f, 0.75f, 1.0f}},
    }};

    inline constexpr std::array<LegendEntry, 2> kPredictedOrbitLegend{{
        {"kPredictionNearColor", "Now", "where a geodesic body goes next", {1.0f, 0.9f, 0.3f}},
        {"kPredictionFarColor", "End of span", "where it is after the prediction span", {0.35f, 0.25f, 0.05f}},
    }};

    inline constexpr std::array<LegendEntry, 2> kTidalAxesLegend{{
        {"kTidalStretchColor", "Stretch", "a principal axis the body is pulled apart along", {1.0f, 0.35f, 0.25f}},
        {"kTidalSqueezeColor", "Squeeze", "a principal axis the body is compressed along", {0.3f, 0.6f, 1.0f}},
    }};

    inline constexpr std::array<LegendEntry, 3> kBodyOrbitLegend{{
        {"kPlanetOrbitColor", "Planets", "osculating orbit about the star (and other bodies whose parent is the heaviest)", {0.30f, 0.55f, 0.95f}},
        {"kMoonOrbitColor", "Moons", "osculating orbit about the parent planet; travels with it", {0.95f, 0.65f, 0.25f}},
        {"kUnboundOrbitColor", "Unbound", "open orbit of a probe or visitor, out to twice its distance", {0.85f, 0.35f, 0.85f}},
    }};

    inline constexpr std::array<OverlayLegend, 7> kOverlayLegends{{
        {"overlay.gravity_grid", kGravityGridLegend},
        {"overlay.physics_colliders", kColliderLegend},
        {"overlay.object_paths", kObjectPathLegend},
        {"overlay.characteristic_radii", kCharacteristicRadiiLegend},
        {"overlay.predicted_orbits", kPredictedOrbitLegend},
        {"overlay.body_orbits", kBodyOrbitLegend},
        {"overlay.tidal_axes", kTidalAxesLegend},
    }};
}
