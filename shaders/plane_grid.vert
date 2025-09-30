#version 460 core
layout(location = 0) in vec3 aPos;

uniform mat4 uVP;

uniform float u_planeY;
uniform float u_displacementScale;
uniform float u_maxDepth;
uniform float u_exponent; // falloff exponent for influence

uniform int u_numBlackHoles;
uniform vec3 u_blackHolePositions[8];
uniform float u_blackHoleMasses[8];

out vec3 vWorldPos;

void main() {
    // Start from the input vertex on the XZ plane
    vec3 p = aPos;
    p.y = u_planeY;

    // Sum influence from all black holes: disp ~ mass / r^exponent
    float disp = 0.0;
    for (int i = 0; i < u_numBlackHoles; ++i) {
        vec3 bh = u_blackHolePositions[i];
        vec2 toBH = p.xz - bh.xz;
        float r = length(toBH);
        // Avoid singularities right on top; small epsilon clamp
        r = max(r, 0.05);
        float contrib = u_blackHoleMasses[i] / pow(r, 2);
        disp += contrib;
    }

    // Scale and clamp depth
    disp = min(disp * u_displacementScale, u_maxDepth);

    // Displace downwards to form the "hole"
    p.y -= disp;

    vWorldPos = p;
    gl_Position = uVP * vec4(p, 1.0);
}

