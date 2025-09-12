#version 460 core
out vec4 FragColor;
void main() {
    vec2 coord = 2.0 * (gl_PointCoord - vec2(0.5));
    float dist = dot(coord, coord);
    if (dist > 1.0) discard;
    FragColor = vec4(1.0, 0.8, 0.2, 1.0);
}

