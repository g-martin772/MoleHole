#version 460 core
layout (location = 0) out vec4 FragColor;

in vec3 vWorldPos;
in float vDispplacement;

uniform float u_cellSize;
uniform float u_lineThickness;
uniform vec3 u_color;
uniform float u_opacity;
uniform int u_numBlackHoles;
uniform vec3 u_blackHolePositions[8];
uniform float u_blackHoleMasses[8];

void main() {
    float cell = max(0.0001, u_cellSize);

    // World position projected onto the XZ plane in cell units
    vec2 uv = vWorldPos.xz / cell;

    // Distance to nearest integer grid line for each axis (0 at line center, 0.5 at cell center)
    vec2 d = abs(fract(uv) - 0.5);

    // Anti-aliased half thickness in parametric (cell) units
    float halfT = clamp(u_lineThickness * 0.5, 0.00025, 0.5);
    float ax = fwidth(uv.x);
    float ay = fwidth(uv.y);

    // Axis-aligned line masks with smooth edges
    float lineX = 1.0 - smoothstep(halfT - ax, halfT + ax, d.x);
    float lineZ = 1.0 - smoothstep(halfT - ay, halfT + ay, d.y);
    float line = max(lineX, lineZ);

    // Gradient color based on effect strength (displacement)
    float effect = clamp(vDispplacement / 10.0, 0.0, 1.0);
    vec3 base = u_color;
    vec3 highlight = vec3(1.0);
    vec3 color = mix(base, highlight, 1.0f);

    float fade = 1.0f - clamp(vDispplacement / 80.0f, 0.0, 1.0);

    FragColor = vec4(u_color, u_opacity * line * 1.0f);
}
