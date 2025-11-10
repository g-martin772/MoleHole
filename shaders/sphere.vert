#version 460 core
layout(location = 0) in vec3 aPos;
uniform mat4 uVP;
uniform mat4 uModel;
uniform float uMass; // Mass of the sphere (in solar masses)
out vec3 Normal;
out float Mass;
void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    gl_Position = uVP * worldPos;
    Normal = mat3(transpose(inverse(uModel))) * aPos;
    Mass = uMass;
}
