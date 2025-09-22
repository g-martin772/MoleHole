#version 460 core

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler3D u_kerrLut;
uniform float u_sliceX; // 0..1 slice along X (distance axis)

// Simple visualization mapping:
// - R channel: deflection.x mapped to hue component
// - G channel: deflection.y mapped to hue component offset
// - B channel: distance factor (B in LUT)
// - Alpha in LUT used to strike invalids (draw as magenta grid)

vec3 hsv2rgb(vec3 c) {
    vec3 rgb = clamp(abs(mod(c.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
    return c.z * mix(vec3(1.0), rgb, c.y);
}

void main() {
    vec3 sampleUV = vec3(clamp(u_sliceX, 0.0, 1.0), TexCoord);
    vec4 lut = texture(u_kerrLut, sampleUV);

    // Deflection angles are typically in radians; map to [-pi, pi] -> [0,1]
    const float PI = 3.14159265359;
    float u = (lut.r / PI) * 0.5 + 0.5; // map to 0..1
    float v = (lut.g / PI) * 0.5 + 0.5;

    // Use two hues for x/y deflection; saturation by validity, value by distance factor
    vec3 colU = hsv2rgb(vec3(u, 1.0, clamp(lut.b, 0.0, 1.0)));
    vec3 colV = hsv2rgb(vec3(v, 1.0, clamp(lut.b, 0.0, 1.0)));
    vec3 color = mix(colU, colV, 0.5);

    // Draw a faint grid to grasp indices
    vec2 grid = fract(TexCoord * 16.0);
    float line = step(grid.x, 0.01) + step(grid.y, 0.01) + step(0.99, grid.x) + step(0.99, grid.y);
    color = mix(color, color * 0.7 + vec3(0.1), clamp(line, 0.0, 1.0));

    // If invalid (A==0), tint magenta overlay
    if (lut.a < 0.5) {
        color = mix(color, vec3(1.0, 0.0, 1.0), 0.6);
    }

    FragColor = vec4(color, 1.0);
}
