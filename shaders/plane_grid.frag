#version 450

layout(location = 0) in vec3 vWorldPos;

layout(location = 0) out vec4 FragColor;

#include "gravity_grid_data.glsl"

void main() {
    float cell = max(0.0001, u_cellSize);

    vec2 uv = vWorldPos.xz / cell;
    vec2 d = abs(fract(uv) - 0.5);

    float halfT = clamp(u_lineThickness * 0.5, 0.00025, 0.5);
    float ax = fwidth(uv.x);
    float ay = fwidth(uv.y);

    float lineX = 1.0 - smoothstep(halfT - ax, halfT + ax, d.x);
    float lineZ = 1.0 - smoothstep(halfT - ay, halfT + ay, d.y);
    float line = max(lineX, lineZ);

    FragColor = vec4(u_color, u_opacity * line);
}
