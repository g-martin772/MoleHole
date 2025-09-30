#version 460 core
layout(location = 0) in vec3 aPos;

uniform mat4 uVP;

uniform float u_planeY;

uniform int u_numBlackHoles;
uniform vec3 u_blackHolePositions[8];
uniform float u_blackHoleMasses[8];

out vec3 vWorldPos;

void main() {
    // Start from the input vertex on the XZ plane
    vec3 p = aPos;
    p.y = u_planeY;

    float disp = 0.0;
    for (int i = 0; i < u_numBlackHoles; ++i) {
        vec3 bh = u_blackHolePositions[i];
        vec2 toBH = p.xz - bh.xz;
        float r = length(toBH);
        r = max(r, 0.05); // avoid numerical issues with divide by zero
        float contrib = u_blackHoleMasses[i] / pow(r, 2);
        disp += contrib;
    }

    p.y -= disp;

    vWorldPos = p;
    gl_Position = uVP * vec4(p, 1.0);
}
