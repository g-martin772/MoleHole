#version 460 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D u_raytracedImage;

void main() {
    vec3 color = texture(u_raytracedImage, TexCoord).rgb;
    FragColor = vec4(color, 1.0);
}
