#include "real.glsl"

const float PHYS_PI = float(R_PI);

const float STEFAN_BOLTZMANN = GPP_STEFAN_BOLTZMANN;

const float G = 1.0f;
const float c = 1.0f;

// Accretion disk annulus in event-horizon radii
const float DISK_INNER_RS = 5.4f;
const float DISK_OUTER_RS = 10.0f;

float calculateEventHorizonRadius(float mass) {
    return 2 * G * mass / pow(c, 2);
}

float calculateInfluenceRadius(float r_s) {
    return 8.0f * r_s;
}

vec3 calculateAngularMomentumFromSpin(float spin, vec3 spinAxis, float mass) {
    float h = spin * mass;
    return h * normalize(spinAxis);
}

vec2 directionToSpherical(vec3 direction) {
    vec3 d = normalize(direction);
    float theta = atan(d.z, d.x);
    float phi = asin(d.y);

    float u = (theta + PHYS_PI) / (2.0 * PHYS_PI);
    float v = (phi + PHYS_PI * 0.5) / PHYS_PI;

    return vec2(u, v);
}
