#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// std430: MoleHole::MeshInstanceGpu (src/rendering/mesh_trace.cppm)
struct MeshInstanceData
{
    mat4 model;
    mat4 normalMatrix;
    vec4 baseColorFactor;
    uint64_t vertexAddress;
    uint64_t indexAddress;
    float metallicFactor;
    float roughnessFactor;
    int textureSlot;
    int _pad0;
};

const int MESH_TEXTURE_SLOTS = 32;
const int MESH_VERTEX_FLOATS = 8;

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer MeshVertexData { float v[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer MeshIndexData { uint i[]; };

layout(set = 0, binding = 6) uniform accelerationStructureEXT u_meshScene;
layout(set = 0, binding = 7, std430) readonly buffer MeshInstanceBlock { MeshInstanceData u_meshInstances[]; };
layout(set = 0, binding = 8) uniform sampler2D u_meshTextures[MESH_TEXTURE_SLOTS];

const float MESH_PI = 3.14159265359;

float meshDistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (MESH_PI * denom * denom);
}

float meshGeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

vec3 shadeMesh(vec3 N, vec3 V, vec4 baseColor, float metallic, float roughness) {
    vec3 L = normalize(vec3(1.0, 1.0, 1.0));
    vec3 H = normalize(V + L);

    vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);
    vec3 F = F0 + (1.0 - F0) * pow(clamp(1.0 - max(dot(H, V), 0.0), 0.0, 1.0), 5.0);
    float NDF = meshDistributionGGX(N, H, roughness);
    float G = meshGeometrySchlickGGX(max(dot(N, V), 0.0), roughness) * meshGeometrySchlickGGX(max(dot(N, L), 0.0), roughness);

    vec3 specular = NDF * G * F / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    float NdotL = max(dot(N, L), 0.0);

    return vec3(0.03) * baseColor.rgb + (kD * baseColor.rgb / MESH_PI + specular) * NdotL;
}

bool meshBoundsHit(vec3 origin, vec3 dir, float tMax) {
    vec3 oc = origin - u_meshBounds.xyz;
    float b = dot(oc, dir);
    float c = dot(oc, oc) - u_meshBounds.w * u_meshBounds.w;
    if (c <= 0.0) return true;
    float disc = b * b - c;
    return b < 0.0 && disc >= 0.0 && -b - sqrt(disc) <= tMax;
}

bool traceMeshes(vec3 origin, vec3 dir, float tMax, out float tHit, out vec3 color) {
    tHit = tMax;
    color = vec3(0.0);
    if (u_numMeshInstances == 0 || !meshBoundsHit(origin, dir, tMax)) return false;

    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_meshScene, gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.0, dir, tMax);
    while (rayQueryProceedEXT(query)) {
    }
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT) return false;

    tHit = rayQueryGetIntersectionTEXT(query, true);
    MeshInstanceData instance = u_meshInstances[rayQueryGetIntersectionInstanceCustomIndexEXT(query, true)];
    int primitive = rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
    vec2 bary = rayQueryGetIntersectionBarycentricsEXT(query, true);
    vec3 weights = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);

    MeshIndexData indices = MeshIndexData(instance.indexAddress);
    MeshVertexData vertices = MeshVertexData(instance.vertexAddress);
    vec3 normal = vec3(0.0);
    vec2 uv = vec2(0.0);
    for (int corner = 0; corner < 3; corner++) {
        int base = int(indices.i[primitive * 3 + corner]) * MESH_VERTEX_FLOATS;
        normal += weights[corner] * vec3(vertices.v[base + 3], vertices.v[base + 4], vertices.v[base + 5]);
        uv += weights[corner] * vec2(vertices.v[base + 6], vertices.v[base + 7]);
    }

    vec4 baseColor = instance.baseColorFactor;
    if (instance.textureSlot >= 0) {
        baseColor *= textureLod(u_meshTextures[nonuniformEXT(instance.textureSlot)], uv, 0.0);
    }
    vec3 N = normalize(mat3(instance.normalMatrix) * normal);
    color = shadeMesh(N, -dir, baseColor, instance.metallicFactor, instance.roughnessFactor);
    return true;
}
