#version 460 core
layout(location = 0) in vec3 aPos;
uniform mat4 uVP;
uniform mat4 uModel;
out vec3 Normal;
void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    gl_Position = uVP * worldPos;
    Normal = mat3(transpose(inverse(uModel))) * aPos;
}

