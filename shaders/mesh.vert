#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 TexCoord;

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
    FragPos = vec3(pc.model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(pc.model))) * aNormal;
    TexCoord = aTexCoord;
    gl_Position = scene.projection * scene.view * vec4(FragPos, 1.0);
}

