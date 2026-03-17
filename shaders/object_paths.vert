#version 460 core

layout(location = 0) in vec3 aPos;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec3 color;
    float opacity;
} pc;

layout(location = 0) out vec3 vColor;
layout(location = 1) out float vOpacity;

void main() {
    vColor = pc.color;
    vOpacity = pc.opacity;
    gl_Position = pc.mvp * vec4(aPos, 1.0);
}
