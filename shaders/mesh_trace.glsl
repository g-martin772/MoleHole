#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#ifdef PBR_DESCRIPTOR_INDEXING
#extension GL_EXT_nonuniform_qualifier : require
#endif

// std430: MoleHole::MeshInstanceGpu (src/rendering/mesh_trace.cppm)
struct MeshInstanceData
{
    mat4 model;
    mat4 normalMatrix;
    uint64_t vertexAddress;
    uint64_t indexAddress;
    uint64_t extraAddress;
    uint materialIndex;
    uint _pad0;
};

const int MESH_VERTEX_FLOATS = 8;
const int MESH_EXTRA_FLOATS = 8;

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer MeshVertexData { float v[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer MeshIndexData { uint i[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer MeshExtraData { float e[]; };

layout(set = 0, binding = 6) uniform accelerationStructureEXT u_meshScene;
layout(set = 0, binding = 7, std430) readonly buffer MeshInstanceBlock { MeshInstanceData u_meshInstances[]; };

#define MESH_SHADOWS 1
#define MESH_BIND_TEXTURES 8
#define MESH_BIND_MATERIALS 9
#define MESH_BIND_LIGHTS 10
#define MESH_BIND_LIGHT_PARAMS 11
#define MESH_BIND_DFG 12
#include "mesh_lighting.glsl"

struct MeshSurface
{
    vec3 normal;
    vec4 tangent;
    vec2 uv0;
    vec2 uv1;
    float lodBase;
};

// Interpolated vertex data of a triangle hit. lodBase is log2 of the pixel footprint in texels at the hit for a
// 1x1 texture: add 0.5 log2(width * height) per texture (ray cone, no curvature).
MeshSurface meshLoadSurface(MeshInstanceData instance, int primitive, vec2 bary, float t, vec3 dir) {
    vec3 weights = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
    MeshIndexData indices = MeshIndexData(instance.indexAddress);
    MeshVertexData vertices = MeshVertexData(instance.vertexAddress);
    MeshExtraData extras = MeshExtraData(instance.extraAddress);

    MeshSurface s;
    s.normal = vec3(0.0);
    s.tangent = vec4(0.0);
    s.uv0 = vec2(0.0);
    s.uv1 = vec2(0.0);
    vec3 corners[3];
    vec2 uvs[3];
    for (int corner = 0; corner < 3; corner++) {
        int index = int(indices.i[primitive * 3 + corner]);
        int base = index * MESH_VERTEX_FLOATS;
        int extra = index * MESH_EXTRA_FLOATS;
        corners[corner] = mat3(instance.model) * vec3(vertices.v[base], vertices.v[base + 1], vertices.v[base + 2]);
        uvs[corner] = vec2(vertices.v[base + 6], vertices.v[base + 7]);
        s.normal += weights[corner] * vec3(vertices.v[base + 3], vertices.v[base + 4], vertices.v[base + 5]);
        s.uv0 += weights[corner] * uvs[corner];
        s.tangent += weights[corner] * vec4(extras.e[extra], extras.e[extra + 1], extras.e[extra + 2], 0.0);
        s.tangent.w = extras.e[extra + 3];
        s.uv1 += weights[corner] * vec2(extras.e[extra + 4], extras.e[extra + 5]);
    }
    s.normal = normalize(mat3(instance.normalMatrix) * s.normal);
    s.tangent = vec4(mat3(instance.model) * s.tangent.xyz, s.tangent.w);

    vec3 e1 = corners[1] - corners[0];
    vec3 e2 = corners[2] - corners[0];
    float worldArea = max(0.5 * length(cross(e1, e2)), 1e-20);
    vec2 d1 = uvs[1] - uvs[0];
    vec2 d2 = uvs[2] - uvs[0];
    float uvArea = max(0.5 * abs(d1.x * d2.y - d1.y * d2.x), 1e-20);
    float pixelAngle = 2.0 * tan(radians(u_fov) * 0.5) / float(imageSize(l_outputImage).y);
    float footprint = max(t * pixelAngle, 1e-20) / sqrt(max(abs(dot(s.normal, dir)), 0.1));
    s.lodBase = log2(footprint) + 0.5 * log2(uvArea / worldArea);
    return s;
}

vec4 meshSampleSlot(GltfTextureSlot slot, MeshSurface surface, vec4 fallback) {
    if (slot.index < 0) return fallback;
    ivec2 size = textureSize(u_meshTextures[PBR_TEXTURE_INDEX(slot.index)], 0);
    float lod = max(surface.lodBase + 0.5 * log2(float(size.x) * float(size.y)), 0.0);
    return textureLod(u_meshTextures[PBR_TEXTURE_INDEX(slot.index)], pbrSlotUv(slot, surface.uv0, surface.uv1), lod);
}

PbrMaterialTexels meshSampleTexels(GltfMaterial m, MeshSurface surface) {
    PbrMaterialTexels t;
    t.baseColor = meshSampleSlot(m.baseColor, surface, vec4(1.0));
    t.metallicRoughness = meshSampleSlot(m.metallicRoughness, surface, vec4(1.0));
    t.normal = meshSampleSlot(m.normal, surface, vec4(0.5, 0.5, 1.0, 1.0)).xyz;
    t.occlusion = meshSampleSlot(m.occlusion, surface, vec4(1.0)).x;
    t.emissive = meshSampleSlot(m.emissive, surface, vec4(1.0)).xyz;
    t.specular = meshSampleSlot(m.specular, surface, vec4(1.0)).w;
    t.specularColor = meshSampleSlot(m.specularColor, surface, vec4(1.0)).xyz;
    return t;
}

bool meshBoundsHit(vec3 origin, vec3 dir, float tMax) {
    vec3 oc = origin - u_meshBounds.xyz;
    float b = dot(oc, dir);
    float c = dot(oc, oc) - u_meshBounds.w * u_meshBounds.w;
    if (c <= 0.0) return true;
    float disc = b * b - c;
    return b < 0.0 && disc >= 0.0 && -b - sqrt(disc) <= tMax;
}

// Alpha test of a candidate hit on a non-opaque instance: single-sided back faces, MASK cutoff and a dithered BLEND
// (the same screen-space threshold as the raster pass).
bool meshAcceptCandidate(rayQueryEXT query, vec3 dir) {
    MeshInstanceData instance = u_meshInstances[rayQueryGetIntersectionInstanceCustomIndexEXT(query, false)];
    GltfMaterial material = u_meshMaterials[instance.materialIndex];
    if (!rayQueryGetIntersectionFrontFaceEXT(query, false) && !pbrDoubleSided(material)) return false;
    uint mode = pbrAlphaMode(material);
    if (mode == PBR_ALPHA_OPAQUE) return true;
    MeshSurface surface = meshLoadSurface(instance, rayQueryGetIntersectionPrimitiveIndexEXT(query, false),
                                          rayQueryGetIntersectionBarycentricsEXT(query, false),
                                          rayQueryGetIntersectionTEXT(query, false), dir);
    float alpha = material.baseColorFactor.a * meshSampleSlot(material.baseColor, surface, vec4(1.0)).a;
    if (mode == PBR_ALPHA_MASK) return alpha >= material.alphaCutoff;
    return alpha > meshDitherThreshold(vec2(gl_GlobalInvocationID.xy));
}

bool traceMeshes(vec3 origin, vec3 dir, float tMax, out float tHit, out vec3 color) {
    tHit = tMax;
    color = vec3(0.0);
    if (u_numMeshInstances == 0 || !meshBoundsHit(origin, dir, tMax)) return false;

    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_meshScene, gl_RayFlagsNoneEXT, 0xFF, origin, 0.0, dir, tMax);
    while (rayQueryProceedEXT(query)) {
        if (rayQueryGetIntersectionTypeEXT(query, false) == gl_RayQueryCandidateIntersectionTriangleEXT
            && meshAcceptCandidate(query, dir)) {
            rayQueryConfirmIntersectionEXT(query);
        }
    }
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT) return false;

    tHit = rayQueryGetIntersectionTEXT(query, true);
    MeshInstanceData instance = u_meshInstances[rayQueryGetIntersectionInstanceCustomIndexEXT(query, true)];
    GltfMaterial material = u_meshMaterials[instance.materialIndex];
    MeshSurface surface = meshLoadSurface(instance, rayQueryGetIntersectionPrimitiveIndexEXT(query, true),
                                          rayQueryGetIntersectionBarycentricsEXT(query, true), tHit, dir);
    vec3 Ng = surface.normal;
    vec4 tangent = surface.tangent;
    if (!rayQueryGetIntersectionFrontFaceEXT(query, true)) {
        Ng = -Ng;
        tangent = vec4(-tangent.xyz, -tangent.w);
    }
    PbrMaterialSample s = pbrEvaluateMaterial(material, meshSampleTexels(material, surface), Ng, tangent);
    color = meshShade(origin + dir * tHit, -dir, s.normal, Ng, s);
    return true;
}
