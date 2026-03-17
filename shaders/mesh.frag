#version 450

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;

layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform SceneData {
    mat4 view;
    mat4 projection;
    vec4 cameraPos;
    vec4 lightDir;
} scene;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
} pc;

void main() {
    vec3 N = normalize(Normal);
    vec3 L = normalize(scene.lightDir.xyz);
    
    // Simple ambient + diffuse
    vec3 ambient = 0.1 * pc.color.rgb;
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * pc.color.rgb;
    
    FragColor = vec4(ambient + diffuse, 1.0);
}
