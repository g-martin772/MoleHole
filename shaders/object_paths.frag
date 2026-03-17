#version 460 core
layout (location = 0) out vec4 FragColor;

in vec3 vColor;
in float vOpacity;

void main() {
    FragColor = vec4(vColor, vOpacity);
}
