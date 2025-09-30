#version 460 core
layout(location = 0) out vec4 FragColor;

in vec3 vWorldPos;

uniform float u_cellSize;        // world units per grid cell
uniform float u_lineThickness;   // fraction of a cell width (e.g., 0.03)
uniform vec3  u_color;
uniform float u_opacity;

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

    float alpha = line * u_opacity;
    if (alpha <= 0.0) discard;

    FragColor = vec4(u_color, alpha);
}
