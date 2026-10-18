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
