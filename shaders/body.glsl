// Surface data of the traced spheres, entry i belongs to u_spheres[i].
// std430: MoleHole::BodyGpu (src/rendering/bodies.cppm). See docs/notes/textured_bodies.md
struct BodyData
{
    vec4 orientation; // unit quaternion (x, y, z, w), body frame -> world; body +Y is the spin axis
    ivec4 maps;       // texture slots: albedo, normal, roughness, emissive; -1 = none
    vec4 tint;        // rgb tint, emissive strength
    vec4 material;    // roughness, normal strength, roughness map is a specular mask, glossy roughness
    vec4 velocity;    // relative to the ZAMO of the first hole along (e_r, e_theta, e_phi), in c; w = 1 when moving
    // Atmosphere in render units (docs/notes/atmospheres.md); atmosphereShape.x = 0: none
    vec4 atmosphereShape; // top radius, Rayleigh scale height, Mie scale height, Mie g
    vec4 rayleigh;        // rgb scattering, w: Mie scattering
    vec4 absorption;      // rgb ozone absorption, w: Mie absorption
    vec4 ozone;           // peak altitude, half width
    vec4 cloudOrientation;
    vec4 cloud;           // shell radius (0: none), opacity, coverage slot (-1: uniform)
    vec4 cloudColor;
    ivec4 flags;          // x: 1 = lit Surface; y: height map slot or -1; z: ocean; w: detail octaves
    vec4 terrain;         // render units: height of black, height of white, detail amplitude, detail scale
    vec4 terrainSeed;     // xyz: noise offset; w: 1 = the body has a Terrain
    vec4 ring;            // inner radius, outer radius (0: none), strip slot, opacity
};

layout(set = 0, binding = 13, std430) readonly buffer BodyBlock { BodyData u_bodies[]; };

// What a body's sky probe saw, reduced by body_probe_reduce.comp (MoleHole::BodyLightingGpu): irradiance as RGB
// spherical harmonics without the brightest source, and that source as a directional light (irradiance.rgb at
// normal incidence, towards direction.xyz). Read from the previous frame while this frame's probes are traced.
struct BodyLighting
{
    vec4 sh[9];
    vec4 direction;
    vec4 irradiance;
};
layout(set = 0, binding = 15, std430) readonly buffer BodyLightingBlock { BodyLighting u_bodyLighting[]; };

// Light components (MoleHole::LightBlockGpu): position.w = 0 directional (xyz towards the light), 1 point (xyz in
// render units); color.rgb irradiance, color.w reference distance of a point light.
struct LightData
{
    vec4 position;
    vec4 color;
};
layout(set = 0, binding = 16, std430) readonly buffer LightBlock {
    ivec4 u_lightHeader; // x: light count, y: 0 fixed lighting, 1 probe + lights, 2 lights only, z: local-frame body or -1,
                         // w: atmosphere samples along a view ray (0: atmospheres off)
    vec4 u_patchCamera;  // xyz: camera relative to the local-frame body, render units along the world axes;
                         // w: terrain march steps
    LightData u_lights[];
};

const int PROBE_WIDTH = 64;
const int PROBE_HEIGHT = 32;

#ifdef GPP_BODY_TEXTURES
#extension GL_EXT_nonuniform_qualifier : require
const int BODY_TEXTURE_SLOTS = 64;
layout(set = 0, binding = 14) uniform sampler2D u_bodyTextures[BODY_TEXTURE_SLOTS];
#endif

const float BODY_PI = 3.14159265359;

vec3 quatRotate(vec4 q, vec3 v) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

vec4 quatConjugate(vec4 q) {
    return vec4(-q.xyz, q.w);
}

// World direction of probe texel (u, v): v = 0 at +Y, u = 0.5 towards +X, east towards -Z (ProbeDirection in
// src/rendering/bodies.cppm).
vec3 probeDirection(vec2 uv) {
    float longitude = (uv.x - 0.5) * 2.0 * BODY_PI;
    float latitude = (0.5 - uv.y) * BODY_PI;
    return vec3(cos(latitude) * cos(longitude), sin(latitude), -cos(latitude) * sin(longitude));
}

// Irradiance from radiance SH coefficients, Ramamoorthi & Hanrahan 2001 eq. 13; basis order of ShBasis
// (src/rendering/bodies.cppm): L00, L1-1 (y), L10 (z), L11 (x), L2-2 (xy), L2-1 (yz), L20, L21 (xz), L22.
vec3 shIrradiance(BodyLighting lit, vec3 n) {
    const float c1 = 0.429043, c2 = 0.511664, c3 = 0.743125, c4 = 0.886227, c5 = 0.247708;
    vec3 e = c1 * lit.sh[8].rgb * (n.x * n.x - n.y * n.y) + c3 * lit.sh[6].rgb * n.z * n.z + c4 * lit.sh[0].rgb
             - c5 * lit.sh[6].rgb
             + 2.0 * c1 * (lit.sh[4].rgb * n.x * n.y + lit.sh[7].rgb * n.x * n.z + lit.sh[5].rgb * n.y * n.z)
             + 2.0 * c2 * (lit.sh[3].rgb * n.x + lit.sh[1].rgb * n.y + lit.sh[2].rgb * n.z);
    return max(e, vec3(0.0));
}

// u = 0 at longitude -180 deg, growing east; v = 0 at the north pole. East longitude = atan(-z, x).
// Mirror of EquirectUv in src/rendering/bodies.cppm.
vec2 equirectUv(vec3 n) {
    float longitude = atan(-n.z, n.x);
    float latitude = asin(clamp(n.y, -1.0, 1.0));
    return vec2(0.5 + longitude / (2.0 * BODY_PI), 0.5 - latitude / BODY_PI);
}

// Mip level from a ray cone of the pixel's angular size (Akenine-Möller et al. 2019, 2021; see
// docs/notes/m2_rendering_references.md): footprint = spread * distance / |n.d|, in texels of the map's latitude
// spacing pi R / height. The distance is the straight one to the camera, so lensing magnification is not included.
float bodyTextureLod(int slot, float footprint, float radius, float cosIncidence) {
#ifdef GPP_BODY_TEXTURES
    float texelSize = BODY_PI * radius / float(textureSize(u_bodyTextures[nonuniformEXT(slot)], 0).y);
    return log2(max(footprint / (texelSize * max(cosIncidence, 0.05)), 1e-6));
#else
    return 0.0;
#endif
}

vec4 sampleBodyMap(int slot, vec2 uv, float footprint, float radius, float cosIncidence, vec4 fallback) {
#ifdef GPP_BODY_TEXTURES
    if (slot < 0) return fallback;
    float lod = bodyTextureLod(slot, footprint, radius, cosIncidence);
    return textureLod(u_bodyTextures[nonuniformEXT(slot)], uv, lod);
#else
    return fallback;
#endif
}

float bodyGgx(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (BODY_PI * denom * denom);
}

float bodySmith(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

// Lambert + GGX (Schlick Fresnel, F0 = 0.04 for dielectrics) for one light of radiance `light` from `L`.
vec3 shadeBodyLight(vec3 N, vec3 V, vec3 L, vec3 light, vec3 albedo, float roughness) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float F = 0.04 + 0.96 * pow(1.0 - max(dot(H, V), 0.0), 5.0);
    float specular = bodyGgx(max(dot(N, H), 0.0), roughness) * bodySmith(NdotV, roughness) * bodySmith(NdotL, roughness)
                     * F / (4.0 * NdotV * NdotL + 1e-4);
    return ((1.0 - F) * albedo / BODY_PI + specular) * light * NdotL;
}

const int MAX_BODY_LIGHTS = 9; // the probe's brightest source + the Light components

// Direct lights at body i, evaluated at world point p: direction towards the light and irradiance at normal
// incidence. Mode 0 is the single fixed light; mode 1 adds the probe's brightest source to the Light components.
int gatherLights(int i, vec3 p, vec3 fixedDir, vec3 fixedIrradiance, out vec3 L[MAX_BODY_LIGHTS],
                 out vec3 E[MAX_BODY_LIGHTS]) {
    int n = 0;
    int mode = u_lightHeader.y;
    if (mode == 0) {
        L[0] = fixedDir;
        E[0] = fixedIrradiance;
        return 1;
    }
    if (mode == 1) {
        BodyLighting lit = u_bodyLighting[i];
        L[n] = lit.direction.xyz;
        E[n] = lit.irradiance.rgb;
        n++;
    }
    for (int l = 0; l < u_lightHeader.x && n < MAX_BODY_LIGHTS; l++) {
        LightData light = u_lights[l];
        L[n] = light.position.xyz;
        E[n] = light.color.rgb;
        if (light.position.w > 0.5) {
            vec3 toLight = light.position.xyz - p;
            float d = max(length(toLight), 1e-6);
            L[n] = toLight / d;
            E[n] *= (light.color.w * light.color.w) / (d * d);
        }
        n++;
    }
    return n;
}

// Mean radiance of the sky around body i without its brightest source (the probe's SH band 0), or the fixed mode's
// faint ambient; feeds the multiple-scattering estimate of atmospheres and the sky light on clouds.
vec3 skyAmbient(int i) {
    if (u_lightHeader.y == 1) return max(u_bodyLighting[i].sh[0].rgb * 0.282095, vec3(0.0));
    return u_lightHeader.y == 0 ? vec3(0.03) : vec3(0.0);
}

// Both crossings of the ray o + t d with a sphere of radius R about the origin; false if it misses.
bool raySphereSpan(vec3 o, vec3 d, float R, out float t0, out float t1) {
    float b = dot(o, d);
    float c = dot(o, o) - R * R;
    float disc = b * b - c;
    if (disc < 0.0) return false;
    float q = sqrt(disc);
    t0 = -b - q;
    t1 = -b + q;
    return true;
}

// ---- Atmosphere (Hillaire 2020; docs/notes/atmospheres.md). Positions relative to the body centre, render units.

vec3 atmosphereExtinction(BodyData b, float h) {
    float rayleighDensity = exp(-h / b.atmosphereShape.y);
    float mieDensity = exp(-h / b.atmosphereShape.z);
    float ozoneDensity = b.ozone.y > 0.0 ? max(0.0, 1.0 - abs(h - b.ozone.x) / b.ozone.y) : 0.0;
    return b.rayleigh.rgb * rayleighDensity + vec3(b.rayleigh.w + b.absorption.w) * mieDensity
           + b.absorption.rgb * ozoneDensity;
}

float rayleighPhase(float mu) {
    return 3.0 / (16.0 * BODY_PI) * (1.0 + mu * mu);
}

// Cornette–Shanks
float miePhase(float mu, float g) {
    float g2 = g * g;
    return 3.0 / (8.0 * BODY_PI) * (1.0 - g2) * (1.0 + mu * mu)
           / ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * g * mu, 1e-6), 1.5));
}

// Transmittance from p to space towards L, zero when the body's surface is in the way.
vec3 atmosphereTransmittance(BodyData b, float radius, vec3 p, vec3 L) {
    float g0, g1;
    if (raySphereSpan(p, L, radius, g0, g1) && g1 > 0.0 && g0 > 0.0) return vec3(0.0);
    float a0, a1;
    if (!raySphereSpan(p, L, b.atmosphereShape.x, a0, a1) || a1 <= 0.0) return vec3(1.0);
    const int samples = 6;
    float ds = a1 / float(samples);
    vec3 depth = vec3(0.0);
    for (int k = 0; k < samples; k++) {
        vec3 x = p + L * ((float(k) + 0.5) * ds);
        depth += atmosphereExtinction(b, length(x) - radius) * ds;
    }
    return exp(-depth);
}

// Light scattered towards the eye along [t0, t1] of the ray o + t d, front to back into (add, transmit).
void atmosphereSegment(int i, vec3 o, vec3 d, float t0, float t1, inout vec3 add, inout vec3 transmit) {
    BodyData b = u_bodies[i];
    int samples = u_lightHeader.w;
    if (b.atmosphereShape.x <= 0.0 || samples <= 0 || t1 <= t0) return;
    float radius = u_spheres[i].radius;
    vec3 L[MAX_BODY_LIGHTS], E[MAX_BODY_LIGHTS];
    int lights = gatherLights(i, u_spheres[i].position, normalize(vec3(1.0)), vec3(BODY_PI), L, E);
    vec3 ambient = skyAmbient(i);
    float ds = (t1 - t0) / float(samples);
    for (int k = 0; k < samples; k++) {
        vec3 x = o + d * (t0 + (float(k) + 0.5) * ds);
        float h = max(length(x) - radius, 0.0);
        vec3 rayleighScatter = b.rayleigh.rgb * exp(-h / b.atmosphereShape.y);
        float mieScatter = b.rayleigh.w * exp(-h / b.atmosphereShape.z);
        vec3 extinction = atmosphereExtinction(b, h);
        vec3 inscatter = (rayleighScatter + vec3(mieScatter)) * ambient; // isotropic multiple-scattering estimate
        for (int l = 0; l < lights; l++) {
            float mu = dot(d, L[l]);
            inscatter += (rayleighScatter * rayleighPhase(mu) + mieScatter * miePhase(mu, b.atmosphereShape.w))
                         * atmosphereTransmittance(b, radius, x, L[l]) * E[l];
        }
        // Analytic integral over the step (Hillaire 2015): S (1 - exp(-sigma ds)) / sigma
        vec3 stepTransmit = exp(-extinction * ds);
        add += transmit * inscatter * (vec3(1.0) - stepTransmit) / max(extinction, vec3(1e-12));
        transmit *= stepTransmit;
    }
}

// The cloud shell of body i where the ray crosses it at body-local x (|x| = shell radius); returns its opacity.
float cloudAt(int i, vec3 x, vec3 d, float eyeDistance, out vec3 radiance) {
    BodyData b = u_bodies[i];
    vec3 n = normalize(x);
    float coverage = 1.0;
    int slot = int(b.cloud.z);
    if (slot >= 0) {
        vec2 uv = equirectUv(quatRotate(quatConjugate(b.cloudOrientation), n));
        float pixelSpread = 2.0 * tan(radians(u_fov) * 0.5) / float(imageSize(l_outputImage).y);
        coverage = sampleBodyMap(slot, uv, pixelSpread * eyeDistance, b.cloud.x, abs(dot(n, d)), vec4(1.0)).r;
    }
    float opacity = clamp(coverage * b.cloud.y, 0.0, 1.0);
    // Diffuse cloud tops; seen from below a thick cloud lets through about a third of what falls on it
    vec3 L[MAX_BODY_LIGHTS], E[MAX_BODY_LIGHTS];
    int lights = gatherLights(i, u_spheres[i].position + x, normalize(vec3(1.0)), vec3(BODY_PI), L, E);
    vec3 irradiance = skyAmbient(i) * BODY_PI;
    float radius = u_spheres[i].radius;
    for (int l = 0; l < lights; l++) {
        vec3 transmitted = b.atmosphereShape.x > 0.0 ? atmosphereTransmittance(b, radius, x, L[l]) : vec3(1.0);
        irradiance += max(dot(n, L[l]), 0.0) * transmitted * E[l];
    }
    float below = dot(d, n) > 0.0 ? 0.35 : 1.0;
    radiance = b.cloudColor.rgb / BODY_PI * irradiance * below;
    return opacity;
}

// Atmosphere and clouds of body i along [tStart, tEnd] of the body-local ray o + t d, front to back into
// (add, transmit): the colour seen is add + transmit * (whatever lies at tEnd). eyeDistance is the eye's distance to o.
void bodyLayers(int i, vec3 o, vec3 d, float tStart, float tEnd, float eyeDistance, inout vec3 add, inout vec3 transmit) {
    BodyData b = u_bodies[i];
    bool hasAtmosphere = b.atmosphereShape.x > 0.0 && u_lightHeader.w > 0;
    bool hasCloud = b.cloud.x > 0.0;
    if (!hasAtmosphere && !hasCloud) return;
    float outer = max(b.atmosphereShape.x, b.cloud.x);
    float s0, s1;
    if (!raySphereSpan(o, d, outer, s0, s1) || s1 <= 0.0) return;
    float start = max(s0, tStart);
    float end = min(s1, tEnd);
    if (end <= start) return;

    // Cloud crossings inside [start, end], in order
    float c[2];
    int crossings = 0;
    float c0, c1;
    if (hasCloud && raySphereSpan(o, d, b.cloud.x, c0, c1)) {
        if (c0 > start && c0 < end) c[crossings++] = c0;
        if (c1 > start && c1 < end) c[crossings++] = c1;
    }
    float from = start;
    for (int k = 0; k <= crossings; k++) {
        float to = k < crossings ? c[k] : end;
        if (hasAtmosphere) atmosphereSegment(i, o, d, from, to, add, transmit);
        if (k < crossings) {
            vec3 radiance;
            float opacity = cloudAt(i, o + d * to, d, eyeDistance + to, radiance);
            add += transmit * opacity * radiance;
            transmit *= 1.0 - opacity;
        }
        from = to;
    }
}

// Where the body-local ray o + t d crosses the ring plane (the body's equator) inside the rings: t and the radial
// coordinate u in [0, 1]; false otherwise.
bool ringCrossing(BodyData b, vec3 o, vec3 d, out float t, out float u) {
    if (b.ring.y <= 0.0) return false;
    vec3 axis = quatRotate(b.orientation, vec3(0.0, 1.0, 0.0));
    float denominator = dot(d, axis);
    if (abs(denominator) < 1e-8) return false;
    t = -dot(o, axis) / denominator;
    if (t <= 0.0) return false;
    float r = length(o + d * t);
    u = (r - b.ring.x) / (b.ring.y - b.ring.x);
    return u >= 0.0 && u <= 1.0;
}

// Colour (rgb) and coverage (a) of the ring strip at radial coordinate u.
vec4 ringSample(BodyData b, float u, float footprint) {
    int slot = int(b.ring.z);
    vec4 strip = vec4(0.8, 0.75, 0.7, 0.8);
#ifdef GPP_BODY_TEXTURES
    if (slot >= 0) {
        // A radial strip: one texel spans (outer - inner) / width across the rings, not the sphere formula's spacing
        float texel = (b.ring.y - b.ring.x) / float(textureSize(u_bodyTextures[nonuniformEXT(slot)], 0).x);
        float lod = log2(max(footprint / texel, 1e-6));
        strip = textureLod(u_bodyTextures[nonuniformEXT(slot)], vec2(u, 0.5), lod);
    }
#endif
    strip.a = clamp(strip.a * b.ring.w, 0.0, 1.0);
    return strip;
}

// The rings at their crossing x: lit on both faces (light passes through the thin layer) unless the body shadows them.
vec3 ringRadiance(int i, vec3 x, vec3 albedo) {
    BodyData b = u_bodies[i];
    vec3 axis = quatRotate(b.orientation, vec3(0.0, 1.0, 0.0));
    vec3 L[MAX_BODY_LIGHTS], E[MAX_BODY_LIGHTS];
    int lights = gatherLights(i, u_spheres[i].position + x, normalize(vec3(1.0)), vec3(BODY_PI), L, E);
    vec3 irradiance = skyAmbient(i) * BODY_PI;
    float radius = u_spheres[i].radius;
    for (int l = 0; l < lights; l++) {
        float g0, g1;
        if (raySphereSpan(x, L[l], radius, g0, g1) && g0 > 0.0) continue; // in the body's shadow
        irradiance += abs(dot(axis, L[l])) * E[l];
    }
    return albedo / BODY_PI * irradiance;
}

// Atmosphere, clouds and rings of body i along [0, tEnd] of the body-local ray o + t d, front to back.
void bodyAllLayers(int i, vec3 o, vec3 d, float tEnd, float eyeDistance, inout vec3 add, inout vec3 transmit) {
    BodyData b = u_bodies[i];
    float tRing, u;
    if (ringCrossing(b, o, d, tRing, u) && tRing < tEnd) {
        bodyLayers(i, o, d, 0.0, tRing, eyeDistance, add, transmit);
        float pixelSpread = 2.0 * tan(radians(u_fov) * 0.5) / float(imageSize(l_outputImage).y);
        vec4 strip = ringSample(b, u, pixelSpread * (eyeDistance + tRing));
        add += transmit * strip.a * ringRadiance(i, o + d * tRing, strip.rgb);
        transmit *= 1.0 - strip.a;
        bodyLayers(i, o, d, tRing, tEnd, eyeDistance, add, transmit);
        return;
    }
    bodyLayers(i, o, d, 0.0, tEnd, eyeDistance, add, transmit);
}

// Direct light from L at surface point x (body-local) after the atmosphere, the cloud shadow and the ring shadow.
vec3 surfaceLightFilter(int i, vec3 x, vec3 L) {
    BodyData b = u_bodies[i];
    vec3 passed = vec3(1.0);
    if (b.atmosphereShape.x > 0.0 && u_lightHeader.w > 0) {
        passed = atmosphereTransmittance(b, u_spheres[i].radius * 0.999, x, L);
    }
    float c0, c1;
    if (b.cloud.x > 0.0 && raySphereSpan(x, L, b.cloud.x, c0, c1) && c1 > 0.0) {
        vec3 shell = x + L * c1;
        float coverage = 1.0;
        int slot = int(b.cloud.z);
        if (slot >= 0) {
            vec2 uv = equirectUv(quatRotate(quatConjugate(b.cloudOrientation), normalize(shell)));
            coverage = sampleBodyMap(slot, uv, 0.0, b.cloud.x, 1.0, vec4(1.0)).r;
        }
        passed *= 1.0 - clamp(coverage * b.cloud.y, 0.0, 1.0);
    }
    float tRing, u;
    if (ringCrossing(b, x, L, tRing, u)) passed *= 1.0 - ringSample(b, u, 0.0).a;
    return passed;
}

#include "terrain.glsl"

// Shaded colour of sphere i where its outward surface normal is N (world axes); rayDir points from the eye to the
// surface, hitDistance is how far the eye is from it (for the texture footprint). lightDir and lightIrradiance are
// the fixed light of the Fixed lighting mode. Positions are not needed: a body rendered in its local frame
// (docs/notes/local_frames.md) has no meaningful world position at the scale of its surface.
vec3 shadeBodySurface(int i, vec3 N, vec3 rayDir, float hitDistance, vec3 baseColor, vec3 lightDir, vec3 lightIrradiance) {
    BodyData body = u_bodies[i];
    vec3 center = u_spheres[i].position;
    float radius = u_spheres[i].radius;
    vec3 hitPoint = center + N * radius; // only for point lights, which are far away compared to the body
    vec3 geometricN = N;
    vec3 V = -rayDir;

    vec4 toBody = quatConjugate(body.orientation);
    vec3 n = quatRotate(toBody, N);
    vec2 uv = equirectUv(n);

    float pixelSpread = 2.0 * tan(radians(u_fov) * 0.5) / float(imageSize(l_outputImage).y);
    float footprint = pixelSpread * hitDistance;
    float cosIncidence = abs(dot(N, V));

    vec3 albedo = baseColor * body.tint.rgb
                  * sampleBodyMap(body.maps.x, uv, footprint, radius, cosIncidence, vec4(1.0)).rgb;

    // Relief from the Terrain, then the normal map on top; water is flat and takes the glossy roughness
    bool water = hasTerrain(body) && terrainIsWater(body, n, radius, footprint);
    vec3 relief = n;
    if (hasTerrain(body) && !water) {
        relief = terrainNormal(body, n, radius, footprint);
        N = quatRotate(body.orientation, relief);
    }

    // Tangent frame in the body frame: east = cross(+Y, n), north = cross(n, east); degenerate at the poles.
    vec3 east = vec3(n.z, 0.0, -n.x);
    float eastLength = length(east);
    if (body.maps.y >= 0 && eastLength > 1e-4 && !water) {
        east /= eastLength;
        vec3 north = cross(n, east);
        vec3 ts = sampleBodyMap(body.maps.y, uv, footprint, radius, cosIncidence, vec4(0.5, 0.5, 1.0, 1.0)).xyz * 2.0 - 1.0;
        ts.xy *= body.material.y;
        vec3 perturbed = normalize(ts.x * east + ts.y * north + max(ts.z, 1e-3) * n + (relief - n));
        N = quatRotate(body.orientation, perturbed);
    }

    float roughness = body.material.x;
    if (body.maps.z >= 0) {
        float map = sampleBodyMap(body.maps.z, uv, footprint, radius, cosIncidence, vec4(1.0)).r;
        roughness = body.material.z > 0.5 ? mix(roughness, body.material.w, map) : roughness * map;
    }
    if (water) roughness = body.material.w;
    roughness = clamp(roughness, 0.02, 1.0);

    // A light below the geometric horizon is blocked by the body itself, whatever the normal map says
    vec3 color = u_lightHeader.y == 0 ? 0.03 * albedo : vec3(0.0);
    if (u_lightHeader.y == 1) color += albedo / BODY_PI * shIrradiance(u_bodyLighting[i], N);
    vec3 L[MAX_BODY_LIGHTS], E[MAX_BODY_LIGHTS];
    int lights = gatherLights(i, hitPoint, lightDir, lightIrradiance, L, E);
    vec3 local = geometricN * radius;
    for (int l = 0; l < lights; l++) {
        if (dot(geometricN, L[l]) <= 0.0) continue;
        color += shadeBodyLight(N, V, L[l], E[l] * surfaceLightFilter(i, local, L[l]), albedo, roughness);
    }
    if (body.maps.w >= 0) {
        color += body.tint.w * sampleBodyMap(body.maps.w, uv, footprint, radius, cosIncidence, vec4(0.0)).rgb;
    }
    return color;
}

vec3 shadeBody(int i, vec3 hitPoint, vec3 rayDir, vec3 baseColor, vec3 lightDir, vec3 lightIrradiance) {
    return shadeBodySurface(i, normalize(hitPoint - u_spheres[i].position), rayDir, distance(u_cameraPos, hitPoint),
                            baseColor, lightDir, lightIrradiance);
}
