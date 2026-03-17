#version 460 core
layout(location = 0) in vec3 aPos;

layout(std140, binding = 0) uniform GravityGridUBO {
    mat4 uVP;
    vec4 u_color; // w = opacity
    vec4 u_params; // x = planeY, y = cellSize, z = lineThickness
    
    // Black Holes
    int u_numBlackHoles;
    vec4 u_blackHolePositions[8];
    float u_blackHoleMasses[8];
    
    // Spheres
    int u_numSpheres;
    vec4 u_spherePositions[16];
    float u_sphereMasses[16];
    
    // Meshes
    int u_numMeshes;
    vec4 u_meshPositions[8];
    float u_meshMasses[8];
};

const float EPSILON = 1e-2f;
const float SOLAR_MASS = 2e30f;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out float vDispplacement;

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
    p.y = u_params.x;

    float displacement = 0.0f;

    for (int i = 0; i < u_numBlackHoles; ++i) {
        vec3 relPos = p - u_blackHolePositions[i].xyz;
        displacement += calculateSpacetimeCurvatureBlackHole(relPos, u_blackHoleMasses[i]);
    }

    for (int i = 0; i < u_numSpheres; ++i) {
        vec3 relPos = p - u_spherePositions[i].xyz;
        float geometricMass = u_sphereMasses[i] * 1.0f / SOLAR_MASS;
        displacement += calculateSpacetimeCurvatureSphere(relPos, geometricMass);
    }

    for (int i = 0; i < u_numMeshes; ++i) {
        vec3 relPos = p - u_meshPositions[i].xyz;
        float geometricMass = u_meshMasses[i] * 1.0f / SOLAR_MASS;
        displacement += calculateSpacetimeCurvatureSphere(relPos, geometricMass);
    }
    
    p.y += displacement;

    vWorldPos = p;
    vDispplacement = displacement;
    gl_Position = uVP * vec4(p, 1.0);
}
