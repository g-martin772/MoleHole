#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#ifdef PBR_DESCRIPTOR_INDEXING
#extension GL_EXT_nonuniform_qualifier : require
#endif
#ifdef MESH_SHADOWS
#extension GL_EXT_ray_query : require
#endif

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
layout(location = 3) in vec4 Tangent;
layout(location = 4) in vec2 TexCoord1;

layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 0, std140) uniform MeshCameraBlock
{
    mat4 u_view;
    mat4 u_projection;
    vec3 u_cameraPos; int u_toneMap;
};

layout(set = 0, binding = 2) uniform sampler2D u_exposureImage;

layout(push_constant) uniform PushConstants
{
    mat4 u_model;
    uint64_t u_extraAddress;
    uint u_materialIndex;
    uint _pad0;
};

#define MESH_BIND_MATERIALS 1
#define MESH_BIND_TEXTURES 3
#define MESH_BIND_LIGHTS 4
#define MESH_BIND_LIGHT_PARAMS 5
#define MESH_BIND_DFG 7
#ifdef MESH_SHADOWS
layout(set = 0, binding = 6) uniform accelerationStructureEXT u_meshScene;
#endif

#include "output_transform.glsl"
#include "mesh_lighting.glsl"

void main() {
    GltfMaterial material = u_meshMaterials[u_materialIndex];
    uint alphaMode = pbrAlphaMode(material);
    bool doubleSided = pbrDoubleSided(material);
    if (!gl_FrontFacing && !doubleSided) discard;

    PbrMaterialTexels texels = pbrSampleMaterialTexels(material, TexCoord, TexCoord1);
    float alpha = material.baseColorFactor.a * texels.baseColor.a;
    if (alphaMode == PBR_ALPHA_MASK && alpha < material.alphaCutoff) discard;
    if (alphaMode == PBR_ALPHA_BLEND && alpha <= meshDitherThreshold(floor(gl_FragCoord.xy))) discard;

    vec3 Ng = normalize(Normal);
    vec3 V = normalize(u_cameraPos - FragPos);
    if (!gl_FrontFacing) Ng = -Ng;
    vec4 tangent = Tangent;
    if (!gl_FrontFacing) tangent = vec4(-tangent.xyz, -tangent.w);
    PbrMaterialSample s = pbrEvaluateMaterial(material, texels, Ng, tangent);
    vec3 color = meshShade(FragPos, V, s.normal, Ng, s);

    FragColor = vec4(displayColor(color, texelFetch(u_exposureImage, ivec2(0), 0).r, u_toneMap), 1.0);
}
