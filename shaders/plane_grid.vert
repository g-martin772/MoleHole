#version 460 core
layout(location = 0) in vec3 aPos;

uniform mat4 uVP;

uniform float u_planeY;

uniform int u_numBlackHoles;
uniform vec3 u_blackHolePositions[8];
uniform float u_blackHoleMasses[8];

out vec3 vWorldPos;
out float vDispplacement;

const float visual_effect_scalar1 = 0.9; // base=1
const float visual_effect_scalar2 = 1; // base=1

void main() {
    // Start from the input vertex on the XZ plane
    vec3 p = aPos;
    p.y = u_planeY;

    float displacement = 0.0;
    for (int i = 0; i < u_numBlackHoles; ++i) {
        vec3 bh = u_blackHolePositions[i];
        float mass = u_blackHoleMasses[i];
        float rs = 2.0 * mass; // Schwarzschild radius
        float r = length(p.xz - bh.xz);
        if (r > rs) {
            float lens = rs / max((r*visual_effect_scalar1) - rs, 0.01);
            displacement += lens;
        }
    }
    p.y -= displacement * visual_effect_scalar2;

    vWorldPos = p;
    vDispplacement = displacement;
    gl_Position = uVP * vec4(p, 1.0);
}
