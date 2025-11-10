#version 460 core
layout(location = 0) in vec3 aPos;

uniform mat4 uVP;

uniform float u_planeY;

uniform int u_numBlackHoles;
uniform vec3 u_blackHolePositions[8];
uniform float u_blackHoleMasses[8];

out vec3 vWorldPos;
out float vDispplacement;

float calculateGravity(vec3 position, float mass) {
    // disregard y component for grid plane
    return (-1.0f * mass) / (pow(position.x, 2) + pow(position.z, 2));
}

void main() {
    // Start from the input vertex on the XZ plane
    vec3 p = aPos;
    p.y = u_planeY;

    float displacement = 0.0f;
    for (int i = 0; i < u_numBlackHoles; ++i) {
        vec3 bh = u_blackHolePositions[i];
        float r = length(p.xz - bh.xz); 
        displacement += 200.0f * u_blackHoleMasses[i] / r;
    }
    p.y -= displacement;

    vWorldPos = p;
    vDispplacement = displacement;
    gl_Position = uVP * vec4(p, 1.0);
}
