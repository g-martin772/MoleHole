// Lighting of meshes, shared by the raster pass (mesh.frag) and the traced pass (mesh_trace.glsl).
// Units and approximations: docs/notes/lighting-materials.md
//
// Before including:
//   PBR_TEXTURE_CAPACITY, optionally PBR_DESCRIPTOR_INDEXING (GPP::PbrShaderDefines)
//   MESH_BIND_MATERIALS, MESH_BIND_LIGHTS, MESH_BIND_LIGHT_PARAMS, MESH_BIND_TEXTURES, MESH_BIND_DFG  binding numbers in set 0
//   MESH_SHADOWS and `uniform accelerationStructureEXT u_meshScene` when ray queries exist
#ifndef MOLEHOLE_MESH_LIGHTING_GLSL
#define MOLEHOLE_MESH_LIGHTING_GLSL

#ifndef PBR_TEXTURE_CAPACITY
#define PBR_TEXTURE_CAPACITY 32
#endif

layout(set = 0, binding = MESH_BIND_TEXTURES) uniform sampler2D u_meshTextures[PBR_TEXTURE_CAPACITY];

// The BRDF's DFG table lives in a buffer: as a constant array indexed per pixel it becomes local memory of every
// thread, which overflows the driver's scratch allocation in the big tracer kernel.
const int MESH_DFG_SIZE = 32;
layout(set = 0, binding = MESH_BIND_DFG, std430) readonly buffer MeshDfgBlock
{
    vec4 u_dfgTable[];
};

vec4 meshDfgLookup(float nov, float roughness) {
    float x = clamp(nov, 0.0, 1.0) * float(MESH_DFG_SIZE - 1);
    float y = clamp(roughness, 0.0, 1.0) * float(MESH_DFG_SIZE - 1);
    int x0 = min(int(x), MESH_DFG_SIZE - 2);
    int y0 = min(int(y), MESH_DFG_SIZE - 2);
    float fx = x - float(x0);
    float fy = y - float(y0);
    int i = y0 * MESH_DFG_SIZE + x0;
    return mix(mix(u_dfgTable[i], u_dfgTable[i + 1], fx),
               mix(u_dfgTable[i + MESH_DFG_SIZE], u_dfgTable[i + MESH_DFG_SIZE + 1], fx), fy);
}
#define PBR_DFG_LOOKUP(nov, roughness) meshDfgLookup(nov, roughness)
#define PBR_MATERIAL_TEXTURES u_meshTextures
#include "material.glsl"

layout(set = 0, binding = MESH_BIND_MATERIALS, std430) readonly buffer MeshMaterialBlock
{
    GltfMaterial u_meshMaterials[];
};

// std430 mirror of GPP::LightGpu (80 bytes)
struct MeshLight
{
    vec3 position;
    float radius;
    vec3 direction;
    uint kind;
    vec3 color;
    float flux;
    vec3 tangent;
    uint flags;
    vec2 halfExtent;
    float cosOuter;
    float cosInner;
};

layout(set = 0, binding = MESH_BIND_LIGHTS, std430) readonly buffer MeshLightBlock
{
    uint u_lightCount;
    uint u_lightsDropped;
    uint u_directionalCount;
    uint _lightPad;
    MeshLight u_lights[];
};

// MoleHole::MeshLightingParamsGpu
layout(set = 0, binding = MESH_BIND_LIGHT_PARAMS, std140) uniform MeshLightingParams
{
    float u_radianceToImage; // image value per W m^-2 sr^-1
    float u_lengthUnit;      // metres per render unit
    float u_skyRadiance;     // uniform sky, W m^-2 sr^-1
    int u_shadowsEnabled;
    float u_shadowBias;      // render units
    float _lightingPad0;
    float _lightingPad1;
    float _lightingPad2;
};

const uint LIGHT_DIRECTIONAL = 0u;
const uint LIGHT_POINT = 1u;
const uint LIGHT_SPOT = 2u;
const uint LIGHT_DISK = 3u;
const uint LIGHT_RECT = 4u;
const uint LIGHT_SPHERE = 5u;
const uint LIGHT_FLAG_TWO_SIDED = 1u;
const uint LIGHT_FLAG_DISK_LOBE = 256u;
const uint MESH_FACE_MASK_OPAQUE = 1u;

#ifdef MESH_SHADOWS
// True when something opaque lies between the origin and origin + dir * tMax (render units).
bool meshShadowed(vec3 origin, vec3 dir, float tMax) {
    rayQueryEXT query;
    rayQueryInitializeEXT(query, u_meshScene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                          MESH_FACE_MASK_OPAQUE, origin, 0.0, dir, tMax);
    while (rayQueryProceedEXT(query)) {
    }
    return rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}
#endif

// Irradiance of one light on a surface at P (render units): the direction to the light L, the irradiance E along it
// (W m^-2, perpendicular to the light), and the distance to it in render units for the shadow ray.
// Area lights use the representative point nearest to the reflection direction.
bool meshLightSample(MeshLight light, vec3 P, vec3 N, vec3 R, out vec3 L, out vec3 E, out float shadowDistance) {
    E = vec3(0.0);
    shadowDistance = 1e30;
    if (light.kind == LIGHT_DIRECTIONAL) {
        L = -normalize(light.direction);
        E = light.color;
        return true;
    }

    vec3 Pm = P * u_lengthUnit;
    vec3 toLight = light.position - Pm;
    float d = length(toLight);
    if (d <= 0.0) return false;
    L = toLight / d;
    vec3 emit = vec3(1.0);

    if (light.kind == LIGHT_POINT || light.kind == LIGHT_SPOT) {
        if ((light.flags & LIGHT_FLAG_DISK_LOBE) != 0u) {
            float innerRadius = light.halfExtent.x;
            float horizonRadius = light.halfExtent.y;
            if (d <= horizonRadius) return false;
            float lobe = abs(dot(normalize(light.direction), L));
            d = max(d, innerRadius);
            emit = vec3(lobe);
        } else if (light.radius > 0.0) {
            vec3 centerToRay = dot(toLight, R) * R - toLight;
            vec3 closest = toLight + centerToRay * clamp(light.radius / max(length(centerToRay), 1e-6), 0.0, 1.0);
            L = normalize(closest);
        }
        if (light.kind == LIGHT_SPOT) {
            float cosAngle = dot(-L, normalize(light.direction));
            float t = clamp((cosAngle - light.cosOuter) / max(light.cosInner - light.cosOuter, 1e-4), 0.0, 1.0);
            emit *= t * t * (3.0 - 2.0 * t);
        }
        E = light.color * emit / (d * d);
        shadowDistance = d / u_lengthUnit;
        return true;
    }

    vec3 axis = normalize(light.direction);
    float emitCos = dot(-L, axis);
    if ((light.flags & LIGHT_FLAG_TWO_SIDED) != 0u) emitCos = abs(emitCos);
    if (light.kind == LIGHT_SPHERE) emitCos = 1.0;
    if (emitCos <= 0.0) return false;
    float area;
    if (light.kind == LIGHT_SPHERE) {
        area = 3.14159265 * light.halfExtent.x * light.halfExtent.x;
        vec3 centerToRay = dot(toLight, R) * R - toLight;
        vec3 closest = toLight + centerToRay * clamp(light.halfExtent.x / max(length(centerToRay), 1e-6), 0.0, 1.0);
        L = normalize(closest);
    } else {
        vec3 tangent = normalize(light.tangent - axis * dot(axis, light.tangent));
        vec3 bitangent = cross(axis, tangent);
        float denom = dot(R, axis);
        vec3 onPlane = toLight;
        if (abs(denom) > 1e-5) {
            float t = dot(toLight, axis) / denom;
            if (t > 0.0) onPlane = R * t;
        }
        vec3 local = onPlane - toLight;
        vec2 plane = vec2(dot(local, tangent), dot(local, bitangent));
        if (light.kind == LIGHT_DISK) {
            float radius = light.halfExtent.x;
            float len = length(plane);
            if (len > radius) plane *= radius / len;
            area = 3.14159265 * radius * radius;
        } else {
            plane = clamp(plane, -light.halfExtent, light.halfExtent);
            area = 4.0 * light.halfExtent.x * light.halfExtent.y;
        }
        vec3 point = toLight + tangent * plane.x + bitangent * plane.y;
        d = length(point);
        L = point / d;
    }
    E = light.color * (area * emitCos) / (d * d);
    shadowDistance = d / u_lengthUnit;
    return true;
}

// Reflected radiance [image units] of the surface s seen from V. N shading normal, Ng geometric normal (both on the
// viewer's side), P render-space position. Emission is added as radiance as well.
vec3 meshShade(vec3 P, vec3 V, vec3 N, vec3 Ng, PbrMaterialSample s) {
    PbrLobes lobes = pbrLobes(s.surface, N, V);
    vec3 R = reflect(-V, N);
    vec3 radiance = vec3(0.0);

    float bias = u_shadowBias + length(P) * 4e-6;
    for (uint i = 0u; i < u_lightCount; i++) {
        MeshLight light = u_lights[i];
        vec3 L;
        vec3 E;
        float shadowDistance;
        if (!meshLightSample(light, P, N, R, L, E, shadowDistance)) continue;
        float nol = dot(N, L);
        if (nol <= 0.0 || dot(Ng, L) <= 0.0 || max(E.r, max(E.g, E.b)) <= 0.0) continue;
#ifdef MESH_SHADOWS
        if (u_shadowsEnabled != 0) {
            vec3 origin = P + Ng * bias;
            if (meshShadowed(origin, L, max(shadowDistance - bias, 0.0))) continue;
        }
#endif
        radiance += evalBsdf(lobes, N, V, L) * E * nol;
    }

    vec3 diffuseAlbedo = lobes.diffuse * (vec3(1.0) - lobes.albedoView);
    radiance += u_skyRadiance * s.occlusion * (lobes.albedoView + diffuseAlbedo);
    return (radiance + s.emission) * u_radianceToImage;
}

// Interleaved gradient noise (Jimenez 2014), a cheap screen-space threshold for dithered transparency.
float meshDitherThreshold(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

#endif
