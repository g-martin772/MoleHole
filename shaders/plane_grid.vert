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

// Calculate spacetime curvature using Schwarzschild metric
// The Schwarzschild metric describes spacetime curvature around a spherical mass
// ds^2 = -(1 - r_s/r)dt^2 + (1 - r_s/r)^-1 dr^2 + r^2 dΩ^2
float calculateSpacetimeCurvature(vec3 position, float mass) {
    float r_s = 2.0f * mass;
    float r = length(position.xz);
    r = max(r, r_s * EPSILON);
    
    // Schwarzschild metric component: the radial curvature factor
    // The proper radial distance is stretched by the metric tensor g_rr = 1/(1 - r_s/r)
    // For embedding diagram visualization, we integrate this to get the "depth"
    // The embedding function is: z(r) = 2*sqrt(r_s) * sqrt(r - r_s) for r > r_s
    float curvature = 0.0f;
    curvature = 2.0f * sqrt(r_s) * sqrt(r - r_s);
    
    return curvature;
}

void main() {
    // Start from the input vertex on the XZ plane
    vec3 p = aPos;
    p.y = u_planeY;

    float displacement = 0.0f;

    // Accumulate curvature from all black holes
    for (int i = 0; i < u_numBlackHoles; ++i) {
        vec3 relPos = p - u_blackHolePositions[i];
        displacement += calculateSpacetimeCurvature(relPos, u_blackHoleMasses[i]);
    }    

    // Accumulate curvature from all spheres
    for (int i = 0; i < u_numSpheres; ++i) {
        vec3 relPos = p - u_spherePositions[i];
        float geometricMass = u_sphereMasses[i] * 1.0f / SOLAR_MASS;
        displacement += calculateSpacetimeCurvature(relPos, geometricMass);
    }

    // Accumulate curvature from all meshes
    for (int i = 0; i < u_numMeshes; ++i) {
        vec3 relPos = p - u_meshPositions[i];
        float geometricMass = u_meshMasses[i] * 1.0f / SOLAR_MASS;
        displacement += calculateSpacetimeCurvature(relPos, geometricMass);
    }
    
    p.y += displacement;

    vWorldPos = p;
    vDispplacement = displacement;
    gl_Position = uVP * vec4(p, 1.0);
}
