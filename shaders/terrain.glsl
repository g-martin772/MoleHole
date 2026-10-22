// Relief of a sphere with a Terrain component (src/rendering/bodies.cppm FillTerrainGpu; docs/notes/terrain.md).
// Heights are in render units above the sphere's radius, n is a body-frame unit direction.
// Included from body.glsl after the texture helpers; needs snoise (noise.glsl).

bool hasTerrain(BodyData b) {
    return b.terrainSeed.w > 0.5;
}

// Ridged fBm detail (Musgrave): octaves of (1 - |simplex|)^2, sharp crests and rounded valleys, the largest with
// wavelength terrain.w and amplitudes halving; mapped to [-amplitude, amplitude].
float terrainDetail(BodyData b, vec3 n, float radius, float footprint) {
    float amplitude = b.terrain.z;
    if (amplitude <= 0.0) return 0.0;
    vec3 p = n * (radius / b.terrain.w) + b.terrainSeed.xyz;
    float wavelength = b.terrain.w;
    float sum = 0.0;
    float total = 0.0;
    float weight = 0.5;
    for (int k = 0; k < b.flags.w; k++) {
        // Octaves smaller than the pixel footprint only alias; fade them to their mean
        float fade = clamp(wavelength / max(footprint * 4.0, 1e-9) - 1.0, 0.0, 1.0);
        float ridge = 1.0 - abs(snoise(p));
        sum += weight * mix(0.5, ridge * ridge, fade);
        total += weight;
        p *= 2.03;
        wavelength *= 0.4926;
        weight *= 0.5;
    }
    return amplitude * (2.0 * sum / total - 1.0);
}

// Land height (before the ocean): the height map between its black and white heights, plus detail.
float terrainLandHeight(BodyData b, vec3 n, float radius, float footprint) {
    float h = 0.0;
    int slot = b.flags.y;
    if (slot >= 0) {
        float value = sampleBodyMap(slot, equirectUv(n), footprint, radius, 1.0, vec4(0.0)).r;
        h = mix(b.terrain.x, b.terrain.y, value);
    }
    return h + terrainDetail(b, n, radius, footprint);
}

// The visible surface: water at sea level (the radius) over land below it when the terrain has an ocean.
float terrainHeight(BodyData b, vec3 n, float radius, float footprint) {
    float h = terrainLandHeight(b, n, radius, footprint);
    return b.flags.z != 0 ? max(h, 0.0) : h;
}

bool terrainIsWater(BodyData b, vec3 n, float radius, float footprint) {
    return b.flags.z != 0 && terrainLandHeight(b, n, radius, footprint) <= 0.0;
}

// Highest and lowest the surface can be, for the bounding shells of the march.
void terrainBounds(BodyData b, out float lowest, out float highest) {
    float detail = abs(b.terrain.z);
    highest = (b.flags.y >= 0 ? max(b.terrain.x, b.terrain.y) : 0.0) + detail;
    lowest = (b.flags.y >= 0 ? min(b.terrain.x, b.terrain.y) : 0.0) - detail;
    if (b.flags.z != 0) lowest = max(lowest, 0.0);
}

// Surface normal of the relief at body-frame direction n, from the height gradient over one footprint (at least a
// texel of the height map). Returns n itself where the relief is flat.
vec3 terrainNormal(BodyData b, vec3 n, float radius, float footprint) {
    if (terrainIsWater(b, n, radius, footprint)) return n;
    vec3 east = vec3(n.z, 0.0, -n.x);
    float eastLength = length(east);
    if (eastLength < 1e-4) return n;
    east /= eastLength;
    vec3 north = cross(n, east);
    float texel = 1.0 / 4096.0;
#ifdef GPP_BODY_TEXTURES
    if (b.flags.y >= 0) texel = BODY_PI / float(textureSize(u_bodyTextures[nonuniformEXT(b.flags.y)], 0).y);
#endif
    float angle = max(footprint / radius, texel);
    float h0 = terrainHeight(b, n, radius, footprint);
    float he = terrainHeight(b, normalize(n + east * angle), radius, footprint);
    float hn = terrainHeight(b, normalize(n + north * angle), radius, footprint);
    float run = angle * radius;
    return normalize(n - east * ((he - h0) / run) - north * ((hn - h0) / run));
}

// Ray o + t d (body-local, render units) against the relief of body i: steps by the height above the surface,
// then bisects the crossing. pixelSpread converts distance into a footprint for the height lookups.
bool terrainIntersect(int i, vec3 o, vec3 d, float pixelSpread, int steps, out float tHit) {
    BodyData b = u_bodies[i];
    float radius = u_spheres[i].radius;
    float lowest, highest;
    terrainBounds(b, lowest, highest);
    float t0, t1;
    if (!raySphereSpan(o, d, radius + highest, t0, t1) || t1 <= 0.0) return false;
    float t = max(t0, 0.0);
    float tEnd = t1;
    float g0, g1;
    if (raySphereSpan(o, d, radius + lowest, g0, g1) && g0 > 0.0) tEnd = min(tEnd, g0 + 1e-6 * radius);

    vec4 toBody = quatConjugate(b.orientation);
    float previous = t;
    for (int k = 0; k < steps; k++) {
        vec3 p = o + d * t;
        float footprint = pixelSpread * t;
        float above = length(p) - radius - terrainHeight(b, quatRotate(toBody, normalize(p)), radius, footprint);
        if (above <= 0.0) {
            float lo = previous;
            float hi = t;
            for (int j = 0; j < 8; j++) {
                float mid = 0.5 * (lo + hi);
                vec3 q = o + d * mid;
                float h = terrainHeight(b, quatRotate(toBody, normalize(q)), radius, pixelSpread * mid);
                if (length(q) - radius - h <= 0.0) hi = mid; else lo = mid;
            }
            tHit = hi;
            return true;
        }
        if (t >= tEnd) return false;
        previous = t;
        // Slopes are assumed below about 1: step a bit less than the height above the surface
        t = min(t + max(above * 0.6, max(pixelSpread * t * 0.5, 1e-7 * radius)), tEnd);
    }
    // Out of steps inside the relief's shell: grazing rays near the horizon; the surface is about here
    tHit = t;
    return true;
}
