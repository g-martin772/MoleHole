#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uVP;
uniform mat4 uModel;

out vec2 vUV;

void main() {
    gl_Position = uVP * uModel * vec4(aPos, 1.0);
    vUV = aUV;
}