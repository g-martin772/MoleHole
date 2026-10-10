#version 450

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 TexCoord;

layout(set = 0, binding = 0, std140) uniform MeshCameraBlock
{
    mat4 u_view;
    mat4 u_projection;
    vec3 u_cameraPos; float _pad0;
    vec3 u_lightDir; float _pad1;
};

layout(push_constant) uniform PushConstants
{
    mat4 u_model;
    vec4 u_baseColorFactor;
    float u_metallicFactor;
    float u_roughnessFactor;
    int u_hasBaseColorTexture;
    float _pad2;
};

void main() {
    FragPos = vec3(u_model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(u_model))) * aNormal;
    TexCoord = aTexCoord;
    gl_Position = u_projection * u_view * vec4(FragPos, 1.0);
}
