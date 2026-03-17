#version 460 core
layout (location = 0) out vec4 FragColor;

layout(location = 0) in vec3 vColor;
layout(location = 1) in float vOpacity;

void main() {
    FragColor = vec4(vColor, vOpacity);
}
