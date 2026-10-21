#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 TexCoord;
layout(location = 3) out vec4 Tangent;
layout(location = 4) out vec2 TexCoord1;

layout(set = 0, binding = 0, std140) uniform MeshCameraBlock
{
    mat4 u_view;
    mat4 u_projection;
    vec3 u_cameraPos; int u_toneMap;
};

layout(push_constant) uniform PushConstants
{
    mat4 u_model;
    uint64_t u_extraAddress;
    uint u_materialIndex;
    uint _pad0;
};

// GPP::GltfVertexExtra: tangent (w = bitangent sign), second UV set
struct VertexExtra { vec4 tangent; vec2 texCoord1; vec2 pad; };
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer ExtraData { VertexExtra v[]; };

void main() {
    FragPos = vec3(u_model * vec4(aPos, 1.0));
    mat3 normalMatrix = mat3(transpose(inverse(u_model)));
    Normal = normalMatrix * aNormal;
    TexCoord = aTexCoord;
    VertexExtra extra = ExtraData(u_extraAddress).v[gl_VertexIndex];
    Tangent = vec4(mat3(u_model) * extra.tangent.xyz, extra.tangent.w);
    TexCoord1 = extra.texCoord1;
    gl_Position = u_projection * u_view * vec4(FragPos, 1.0);
}
