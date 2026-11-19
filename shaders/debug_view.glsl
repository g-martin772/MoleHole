const int TERM_SKY = 0;
const int TERM_HORIZON = 1;
const int TERM_OBJECT = 2;
const int TERM_DISK = 3;
const int TERM_STEPS = 4;
const int TERM_CROSSHAIR = 5;
int g_stepCount = 0;
int g_maxStepCount = 1;
int g_termination = TERM_SKY;
// Largest |2H| / E^2 along the ray, written by the integrated tracer (debug view 3).
float g_constraintDrift = 0.0;

// Debug view 4 overlays, drawn by the integrated tracer.
const vec3 ERGOSPHERE_TINT = vec3(0.6, 0.0, 0.8);
const vec3 ISCO_RING_COLOR = vec3(0.0, 1.0, 0.0);

// overlay.characteristic_radii in the integrated tracer; same colours as src/rendering/orbit_overlays.cppm
const vec3 ORBIT_PHOTON_COLOR = vec3(1.0, 0.6, 0.15);
const vec3 ORBIT_BOUND_COLOR = vec3(0.35, 0.9, 0.4);
const vec3 ORBIT_ISCO_COLOR = vec3(0.2, 0.75, 1.0);
const vec3 ORBIT_ERGOSPHERE_COLOR = vec3(0.65, 0.35, 1.0);
const float ORBIT_RETROGRADE_DIMMING = 0.5;
// Ring half-width as a fraction of its radius, and how strongly the ergoregion is tinted
const float ORBIT_RING_WIDTH = 0.012;
const float ORBIT_ERGOSPHERE_TINT = 0.35;

// Colour of the characteristic-radius rings at an equatorial crossing at Boyer–Lindquist radius r (black if none)
vec3 orbitRingColor(float r) {
    vec3 color = vec3(0.0);
    float width = ORBIT_RING_WIDTH * r;
    if (abs(r - u_orbitRadii.x) < width) color += ORBIT_PHOTON_COLOR;
    if (abs(r - u_orbitRadii.y) < width) color += ORBIT_PHOTON_COLOR * ORBIT_RETROGRADE_DIMMING;
    if (abs(r - u_orbitRadii.z) < width) color += ORBIT_BOUND_COLOR;
    if (abs(r - u_orbitRadii.w) < width) color += ORBIT_BOUND_COLOR * ORBIT_RETROGRADE_DIMMING;
    if (abs(r - u_orbitRadii2.x) < width) color += ORBIT_ISCO_COLOR;
    if (abs(r - u_orbitRadii2.y) < width) color += ORBIT_ISCO_COLOR * ORBIT_RETROGRADE_DIMMING;
    return color;
}

// debug.sky = Checkerboard (kept below the default bloom threshold of 0.6 so it does not glow): a 10 degree latitude/longitude grid around the first hole's spin axis (world +y without a
// hole). One hue per longitude quadrant, full brightness in the northern hemisphere and SKY_SOUTH_DIMMING in the south;
// cells alternate with SKY_CELL_CONTRAST. The celestial equator and the four quadrant meridians are SKY_LINE_COLOR.
// Mirrored by kSkyCheckerboardLegend in src/rendering/debug_legend.cppm.
const vec3 SKY_QUADRANT_0 = vec3(0.48, 0.13, 0.10);
const vec3 SKY_QUADRANT_1 = vec3(0.11, 0.40, 0.14);
const vec3 SKY_QUADRANT_2 = vec3(0.13, 0.21, 0.53);
const vec3 SKY_QUADRANT_3 = vec3(0.50, 0.42, 0.08);
const vec3 SKY_LINE_COLOR = vec3(0.55, 0.55, 0.55);
const float SKY_CELL_DEGREES = 10.0;
const float SKY_CELL_CONTRAST = 0.55;
const float SKY_SOUTH_DIMMING = 0.45;
const float SKY_LINE_HALF_WIDTH = 0.004; // radians

vec3 checkerboardSky(vec3 direction) {
    vec3 axis = vec3(0.0, 1.0, 0.0);
    if (u_numBlackHoles > 0 && dot(u_blackHoles[0].spinAxis, u_blackHoles[0].spinAxis) > 0.0) {
        axis = normalize(u_blackHoles[0].spinAxis);
    }
    // Same longitude origin as spinFrame in shaders/kerr.glsl
    vec3 helper = abs(axis.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 xAxis = normalize(cross(helper, axis));
    vec3 yAxis = cross(axis, xAxis);
    vec3 d = normalize(direction);
    float latitude = asin(clamp(dot(d, axis), -1.0, 1.0));
    float longitude = atan(dot(d, yAxis), dot(d, xAxis)); // -pi .. pi

    float halfPi = 1.5707963;
    float meridian = abs(longitude / halfPi - round(longitude / halfPi)) * halfPi * cos(latitude);
    if (abs(latitude) < SKY_LINE_HALF_WIDTH || meridian < SKY_LINE_HALF_WIDTH) return SKY_LINE_COLOR;

    int quadrant = clamp(int(floor((longitude + 2.0 * halfPi) / halfPi)), 0, 3);
    vec3 color = quadrant == 0 ? SKY_QUADRANT_0 : quadrant == 1 ? SKY_QUADRANT_1 : quadrant == 2 ? SKY_QUADRANT_2 : SKY_QUADRANT_3;
    float cell = radians(SKY_CELL_DEGREES);
    int parity = (int(floor(longitude / cell)) + int(floor(latitude / cell))) & 1;
    if (parity == 1) color *= SKY_CELL_CONTRAST;
    if (latitude < 0.0) color *= SKY_SOUTH_DIMMING;
    return color;
}

// What a ray that escapes in `direction` brings back
vec3 skyRadiance(vec3 direction) {
    if (u_skyMode == 1) return checkerboardSky(direction);
    return texture(u_skyboxTexture, directionToSpherical(direction)).rgb;
}

// Debug view 5: the order of the disk image a pixel shows, counted as crossings of the first hole's equatorial plane
// up to the disk: 1 is the direct image, 2 the image of the far side bent under or over the hole, 3 and more the
// photon rings. Pixels without disk show how often their ray crossed the plane at all, dimmed by
// IMAGE_ORDER_SKY_DIMMING, or IMAGE_ORDER_CAPTURED_DIMMING for rays that fell in. Mirrored by kImageOrderLegend.
const vec3 IMAGE_ORDER_0 = vec3(0.06, 0.06, 0.14);
const vec3 IMAGE_ORDER_1 = vec3(0.95, 0.55, 0.10);
const vec3 IMAGE_ORDER_2 = vec3(0.20, 0.65, 1.00);
const vec3 IMAGE_ORDER_3 = vec3(0.35, 0.95, 0.35);
const vec3 IMAGE_ORDER_4 = vec3(1.00, 1.00, 1.00);
const float IMAGE_ORDER_SKY_DIMMING = 0.4;
const float IMAGE_ORDER_CAPTURED_DIMMING = 0.2;

vec3 imageOrderColor(int crossings) {
    if (crossings <= 0) return IMAGE_ORDER_0;
    if (crossings == 1) return IMAGE_ORDER_1;
    if (crossings == 2) return IMAGE_ORDER_2;
    if (crossings == 3) return IMAGE_ORDER_3;
    return IMAGE_ORDER_4;
}

// Debug views 6-8: log2 of the disk's frequency ratio g = nu_camera / nu_emitted, opacity-weighted over the disk
// samples of the ray: red for redshift (g < 1), blue for blueshift (g > 1), white for none, saturated at
// |log2 g| = REDSHIFT_LOG2_RANGE. Pixels without disk show the image in grey. Mirrored by DebugDiverging.
const vec3 REDSHIFT_RED = vec3(0.85, 0.12, 0.08);
const vec3 REDSHIFT_NEUTRAL = vec3(0.95, 0.95, 0.95);
const vec3 REDSHIFT_BLUE = vec3(0.10, 0.35, 0.95);
const float REDSHIFT_LOG2_RANGE = 1.0;
const float REDSHIFT_CONTEXT_GREY = 0.25;

vec3 debugDiverging(float t) {
    t = clamp(t, -1.0, 1.0);
    return t < 0.0 ? mix(REDSHIFT_NEUTRAL, REDSHIFT_RED, -t) : mix(REDSHIFT_NEUTRAL, REDSHIFT_BLUE, t);
}

vec3 redshiftViewColor(int mode, vec3 shaded) {
    vec3 context = vec3(REDSHIFT_CONTEXT_GREY * dot(shaded / (1.0 + shaded), vec3(0.2126, 0.7152, 0.0722)));
    if (g_diskWeight <= 1e-4) return context;
    float total = g_diskLog2G / g_diskWeight;
    float gravity = g_diskLog2GGravity / g_diskWeight;
    float value = mode == 6 ? total : mode == 7 ? gravity : total - gravity;
    return mix(context, debugDiverging(value / REDSHIFT_LOG2_RANGE), smoothstep(0.0, 0.5, g_diskWeight));
}

vec3 debugHeat(float t) {
    t = clamp(t, 0.0, 1.0);
    return clamp(vec3(1.5 * t - 0.25, 1.5 - abs(3.0 * t - 1.5), 1.25 - 1.5 * t), 0.0, 1.0);
}

vec3 debugViewColor(vec3 shaded) {
    if (u_debugMode == 1) {
        return debugHeat(sqrt(float(g_stepCount) / float(max(g_maxStepCount, 1))));
    }
    if (u_debugMode == 3) {
        // log10 scale: 1e-6 -> 0, 1 -> 1
        return debugHeat((log(max(g_constraintDrift, 1e-8)) / log(10.0) + 6.0) / 6.0);
    }
    if (u_debugMode == 5) {
        if (g_diskOrder > 0) return imageOrderColor(g_diskOrder);
        vec3 crossings = imageOrderColor(g_equatorCrossings);
        return crossings * (g_termination == TERM_HORIZON ? IMAGE_ORDER_CAPTURED_DIMMING : IMAGE_ORDER_SKY_DIMMING);
    }
    if (u_debugMode >= 6 && u_debugMode <= 8) {
        return redshiftViewColor(u_debugMode, shaded);
    }
    if (u_debugMode == 2) {
        if (g_termination == TERM_SKY) return vec3(0.04, 0.08, 0.22);
        if (g_termination == TERM_HORIZON) return vec3(0.9, 0.05, 0.05);
        if (g_termination == TERM_OBJECT) return vec3(0.1, 0.8, 0.2);
        if (g_termination == TERM_DISK) return vec3(1.0, 0.6, 0.0);
        if (g_termination == TERM_STEPS) return vec3(1.0, 1.0, 1.0);
        return vec3(0.0, 0.9, 0.9);
    }
    return shaded;
}
