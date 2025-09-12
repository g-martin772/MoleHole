#version 460 core
in vec3 Normal;
out vec4 FragColor;
uniform vec3 uColor;
void main() {
    float lighting = 0.5 + 0.5 * Normal.z;
    FragColor = vec4(uColor * lighting, 1.0);
}

