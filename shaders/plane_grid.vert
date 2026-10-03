#version 450

layout(location = 0) in vec3 aPos;

layout(location = 0) out vec3 vWorldPos;

#include "gravity_grid_data.glsl"

const float EPSILON = 1e-2f;

float calculateSpacetimeCurvatureBlackHole(vec3 position, float mass) {
    float r_s = 2.0f * mass;
    float r = length(position.xz);
    r = max(r, r_s * EPSILON);
    return 2.0f * sqrt(r_s) * sqrt(r - r_s);
}

float calculateSpacetimeCurvatureSphere(vec3 position, float mass) {
    float r_s = 2.0f * mass;
    float r = length(position.xz);
    r = max(r, r_s * EPSILON);

    if (r > r_s) {
        return 2.0f * sqrt(r_s) * sqrt(r - r_s);
    }
    float depth_at_rs = 2.0f * sqrt(r_s) * sqrt(EPSILON * r_s);
    float normalized_r = r / r_s;
    return depth_at_rs + (normalized_r * normalized_r - 1.0f) * r_s * 0.5f;
}

void main() {
    vec3 p = aPos;
    p.y = u_planeY;

    float displacement = 0.0f;

    for (int i = 0; i < u_numBlackHoles; ++i) {
        vec3 relPos = p - u_blackHoles[i].position;
        displacement += calculateSpacetimeCurvatureBlackHole(relPos, u_blackHoles[i].mass);
    }

    for (int i = 0; i < u_numSpheres; ++i) {
        vec3 relPos = p - u_spheres[i].position;
        displacement += calculateSpacetimeCurvatureSphere(relPos, u_spheres[i].mass);
    }

    for (int i = 0; i < u_numMeshes; ++i) {
        vec3 relPos = p - u_meshes[i].position;
        displacement += calculateSpacetimeCurvatureSphere(relPos, u_meshes[i].mass);
    }

    p.y += displacement;

    vWorldPos = p;
    gl_Position = u_VP * vec4(p, 1.0);
}
