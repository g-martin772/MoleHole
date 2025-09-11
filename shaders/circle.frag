#version 460 core
in vec2 vPos;
out vec4 FragColor;
void main() {
    float dist = length(vPos);
    if (dist < 0.5)
        FragColor = vec4(0.2, 0.7, 1.0, 1.0);
    else
        FragColor = vec4(0.0, 0.0, 0.0, 0.0);
}

