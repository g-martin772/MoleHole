#version 460 core
layout (location = 0) out vec4 FragColor;

in vec3 vWorldPos;

uniform vec3 u_color;
uniform float u_opacity;

void main() {
    FragColor = vec4(u_color, u_opacity);
}
