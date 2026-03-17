#version 460 core
layout (location = 0) out vec4 FragColor;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in float vDispplacement;

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

void main() {
    float cell = max(0.0001, u_params.y);

    // World position projected onto the XZ plane in cell units
    vec2 uv = vWorldPos.xz / cell;

    // Distance to nearest integer grid line for each axis (0 at line center, 0.5 at cell center)
    vec2 d = abs(fract(uv) - 0.5);

    // Anti-aliased half thickness in parametric (cell) units
    float halfT = clamp(u_params.z * 0.5, 0.00025, 0.5);
    float ax = fwidth(uv.x);
    float ay = fwidth(uv.y);

    // Axis-aligned line masks with smooth edges
    float lineX = 1.0 - smoothstep(halfT - ax, halfT + ax, d.x);
    float lineZ = 1.0 - smoothstep(halfT - ay, halfT + ay, d.y);
    float line = max(lineX, lineZ);

    // Gradient color based on effect strength (displacement)
    float effect = clamp(vDispplacement / 10.0, 0.0, 1.0);
    vec3 base = u_color.rgb;
    vec3 highlight = vec3(1.0);
    vec3 color = mix(base, highlight, effect * 0.5f); // Use effect

    float fade = 1.0f - clamp(vDispplacement / 80.0f, 0.0, 1.0);

    FragColor = vec4(color, u_color.w * line * fade); // Use fade and mixed color
}
