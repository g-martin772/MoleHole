#version 460 core

out vec3 v_Color;

uniform mat4 u_ViewProjection;

layout (location = 1) in vec3 a_Color;
layout (location = 0) in vec3 a_Position;

void main() {
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
    v_Color = a_Color;
}
