#version 460 core

in vec2 v_texCoord;
out vec4 FragColor;

uniform sampler2D u_texture;
uniform vec4 u_color;

void main() {
    float alpha = texture(u_texture, v_texCoord).r;
    FragColor = vec4(u_color.rgb, u_color.a * alpha);
}

