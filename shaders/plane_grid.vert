#version 460 core
layout(location = 0) in vec3 aPos;

uniform mat4 uVP;

uniform float u_planeY;

const float EPSILON = 1e-2f;
const float SOLAR_MASS = 2e30f;

// Black holes
const int MAX_BLACK_HOLES = 8;
uniform int u_numBlackHoles;
uniform vec3 u_blackHolePositions[MAX_BLACK_HOLES];
uniform float u_blackHoleMasses[MAX_BLACK_HOLES];

// Spheres
const int MAX_SPHERES = 16;
uniform int u_numSpheres;
uniform vec3 u_spherePositions[MAX_SPHERES];
uniform float u_sphereMasses[MAX_SPHERES];

// Meshes
const int MAX_MESHES = 8;
uniform int u_numMeshes;
uniform vec3 u_meshPositions[MAX_MESHES];
uniform float u_meshMasses[MAX_MESHES];

out vec3 vWorldPos;
out float vDispplacement;

float calculateSpacetimeCurvatureBlackHole(vec3 position, float mass) {
    float r_s = 2.0f * mass;
    float r = length(position.xz);
    r = max(r, r_s * EPSILON);

    float curvature = 0.0f;
    curvature = 2.0f * sqrt(r_s) * sqrt(r - r_s);

    return curvature;
}

float calculateSpacetimeCurvatureSphere(vec3 position, float mass) {
    float r_s = 2.0f * mass;
    float r = length(position.xz);
    r = max(r, r_s * EPSILON);

    float curvature = 0.0f;
    if (r > r_s) {
        curvature = 2.0f * sqrt(r_s) * sqrt(r - r_s);
    } else {
        float depth_at_rs = 2.0f * sqrt(r_s) * sqrt(EPSILON * r_s);
        float normalized_r = r / r_s;
        curvature = depth_at_rs + (normalized_r * normalized_r - 1.0f) * r_s * 0.5f;
    }

    return curvature;
}

void main() {
    // Start from the input vertex on the XZ plane
    vec3 p = aPos;
    p.y = u_planeY;

    float displacement = 0.0f;

    for (int i = 0; i < u_numBlackHoles; ++i) {
        vec3 relPos = p - u_blackHolePositions[i];
        displacement += calculateSpacetimeCurvatureBlackHole(relPos, u_blackHoleMasses[i]);
    }

    for (int i = 0; i < u_numSpheres; ++i) {
        vec3 relPos = p - u_spherePositions[i];
        float geometricMass = u_sphereMasses[i] * 1.0f / SOLAR_MASS;
        displacement += calculateSpacetimeCurvatureSphere(relPos, geometricMass);
    }

    for (int i = 0; i < u_numMeshes; ++i) {
        vec3 relPos = p - u_meshPositions[i];
        float geometricMass = u_meshMasses[i] * 1.0f / SOLAR_MASS;
        displacement += calculateSpacetimeCurvatureSphere(relPos, geometricMass);
    }
    
    p.y += displacement;

    vWorldPos = p;
    vDispplacement = displacement;
    gl_Position = uVP * vec4(p, 1.0);
}
