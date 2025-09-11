#version 460 core
in vec2 vPos;
out vec4 FragColor;
void main() {
    float t = (vPos.x + 1.0) * 0.5;
    FragColor = mix(vec4(0.2, 0.2, 0.8, 1.0), vec4(0.8, 0.8, 0.2, 1.0), t);
}

