#ifndef PARTICLE_GRID_GLSL
#define PARTICLE_GRID_GLSL

// Nested Cartesian grids of the particle disk (clip-map, level 0 fine, level 1 coarse), centred on the hole, axes of
// the world, lengths in r_g of the hole. Built by particle_splat.comp, sampled by the tracers. Notes: docs/notes/particle-disk.md

#ifndef PARTICLE_GRID_BINDING
#define PARTICLE_GRID_BINDING 13
#endif

layout(set = 0, binding = PARTICLE_GRID_BINDING, std140) uniform ParticleGridBlock
{
    vec4 u_particleMin[2]; // xyz: minimum corner of the level [r_g], w: voxel size [r_g]
    vec4 u_particleInfo;   // x: voxels per axis, y: opacity kappa * scale [m^2/kg], z: reference temperature [K], w: tracer index of the hole (-1: none)
    vec4 u_particleFrame;  // x: r_g [m], y: fixed-point units per kg (0: float atomics), z: kg per fixed-point unit, w: mean particle mass [kg]
    vec4 u_particleSmooth; // x: particles per cell the 3D density estimate asks for (0: no adaptive smoothing), y: coarsest 3D mip, z: step jitter (0 or 1), w: particles per cell of the disk grid
    // Disk gas (near circular, near the mid-plane) lives in a 2D grid of column densities with an analytic vertical profile
    vec4 u_planeNormal;    // xyz: disk normal, w: aspect ratio H/R (0: no disk grid)
    vec4 u_planeE1;        // xyz: first axis of the plane grid, w: inner radius of disk gas [r_g]
    vec4 u_planeE2;        // xyz: second axis of the plane grid, w: outer radius of disk gas [r_g]
    vec4 u_planeGrid;      // x: minimum corner (both axes) [r_g], y: cell size [r_g], z: cells per axis, w: coarsest mip
    vec4 u_planeInfo;      // x: Schwarzschild radius [r_g]
};

#ifndef PARTICLE_GRID_NO_TEXTURES
// Per level: (D, D T / T_ref, D v_x, D v_y) and (D v_z), D = rho r_g [kg/m^2], v in units of c
layout(set = 0, binding = 14) uniform sampler3D u_particleGrid0A;
layout(set = 0, binding = 15) uniform sampler3D u_particleGrid0B;
layout(set = 0, binding = 16) uniform sampler3D u_particleGrid1A;
layout(set = 0, binding = 17) uniform sampler3D u_particleGrid1B;
// Disk grid: (Sigma, Sigma T / T_ref, Sigma v_x, Sigma v_y) and (Sigma v_z, Sigma s2) per plane area [kg/m^2], s2 = (z / (h R))^2
layout(set = 0, binding = 18) uniform sampler2D u_diskGridA;
layout(set = 0, binding = 19) uniform sampler2D u_diskGridB;

float particleExtent() {
    return -u_particleMin[1].x;
}

float particleVoxel(vec3 p) {
    return all(lessThan(abs(p), vec3(-u_particleMin[0].x))) ? u_particleMin[0].w : u_particleMin[1].w;
}

// One level at the coarsest-needed resolution. The density at a coarse mip says how many particles a cell holds there;
// the mip is the finest one whose cell holds u_particleSmooth.x of them, blended trilinearly (adaptive kernel density
// estimate: sparse gas is smoothed over larger cells, dense gas keeps the full resolution).
void particleFetchLevel(int level, vec3 uvw, out vec4 a, out float b) {
    float lod = 0.0;
    float target = u_particleSmooth.x;
    if (target > 0.0) {
        float maxLod = u_particleSmooth.y;
        float probe = min(2.0, maxLod);
        float Dp = level == 0 ? textureLod(u_particleGrid0A, uvw, probe).x : textureLod(u_particleGrid1A, uvw, probe).x;
        float voxel = u_particleMin[level].w;
        float perVoxel = Dp * u_particleFrame.x * u_particleFrame.x * voxel * voxel * voxel / max(u_particleFrame.w, 1e-30);
        lod = perVoxel > 1e-12 ? clamp(log2(target / perVoxel) * (1.0 / 3.0), 0.0, maxLod) : maxLod;
    }
    if (level == 0) {
        a = textureLod(u_particleGrid0A, uvw, lod);
        b = textureLod(u_particleGrid0B, uvw, lod).r;
    } else {
        a = textureLod(u_particleGrid1A, uvw, lod);
        b = textureLod(u_particleGrid1B, uvw, lod).r;
    }
}

// Disk gas at p: column density of the plane grid times a Gaussian profile of height H = h R (scaled by the measured
// spread of the particles of the cell, within 0.7 to 1.5). The plane grid is read at the mip whose cell holds
// u_particleSmooth.w particles. Extensive like the 3D grid, so the two add.
void sampleDiskGas(vec3 p, out float D, out float DT, out vec3 Dv) {
    D = 0.0;
    DT = 0.0;
    Dv = vec3(0.0);
    float h = u_planeNormal.w;
    if (h <= 0.0) return;
    float z = dot(p, u_planeNormal.xyz);
    vec3 inPlane = p - z * u_planeNormal.xyz;
    vec2 xy = vec2(dot(inPlane, u_planeE1.xyz), dot(inPlane, u_planeE2.xyz));
    float R = length(xy);
    float width = u_planeGrid.z * u_planeGrid.y;
    vec2 uv = (xy - u_planeGrid.x) / width;
    if (any(lessThanEqual(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) return;
    float sigma0 = max(h * R, 1e-3);
    if (abs(z) > 7.0 * sigma0) return;
    float lod = 0.0;
    float target = u_particleSmooth.w;
    if (target > 0.0) {
        float maxLod = u_planeGrid.w;
        float probe = min(2.0, maxLod);
        float cell = u_planeGrid.y;
        float perCell = textureLod(u_diskGridA, uv, probe).x * u_particleFrame.x * u_particleFrame.x * cell * cell / max(u_particleFrame.w, 1e-30);
        lod = perCell > 1e-12 ? clamp(log2(target / perCell) * 0.5, 0.0, maxLod) : maxLod;
    }
    vec4 a = textureLod(u_diskGridA, uv, lod);
    if (!(a.x > 0.0)) return;
    vec2 b = textureLod(u_diskGridB, uv, lod).xy;
    float sigma = sigma0;
    float zs = z / sigma;
    float edge = smoothstep(0.0, 0.04, min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y)));
    D = a.x * 0.3989422804 / sigma * exp(-0.5 * zs * zs) * edge;
    float inv = 1.0 / a.x;
    DT = D * a.y * inv;
    Dv = D * vec3(a.z * inv, a.w * inv, b.x * inv);
}

// Extensive gas quantities at p [r_g, hole frame]; false outside the coarse level. The fine level takes over inside,
// blended over its last voxels where the kernel of the splat is cut off by the grid edge.
bool sampleParticleGrid(vec3 p, out float D, out float DT, out vec3 Dv) {
    D = 0.0;
    DT = 0.0;
    Dv = vec3(0.0);
    float res = u_particleInfo.x;
    vec3 uvwC = (p - u_particleMin[1].xyz) / (res * u_particleMin[1].w);
    if (any(lessThanEqual(uvwC, vec3(0.0))) || any(greaterThanEqual(uvwC, vec3(1.0)))) return false;
    vec3 uvwF = (p - u_particleMin[0].xyz) / (res * u_particleMin[0].w);
    vec3 toEdge = min(uvwF, 1.0 - uvwF);
    float edge = min(toEdge.x, min(toEdge.y, toEdge.z));
    float wFine = smoothstep(1.5 / res, 5.5 / res, edge);
    vec4 a = vec4(0.0);
    float b = 0.0;
    if (wFine < 1.0) {
        particleFetchLevel(1, uvwC, a, b);
    }
    if (wFine > 0.0) {
        vec4 aF;
        float bF;
        particleFetchLevel(0, uvwF, aF, bF);
        a = mix(a, aF, wFine);
        b = mix(b, bF, wFine);
    }
    vec3 toEdgeC = min(uvwC, 1.0 - uvwC);
    float fade = smoothstep(0.0, 0.08, min(toEdgeC.x, min(toEdgeC.y, toEdgeC.z)));
    D = a.x * fade;
    DT = a.y * fade;
    Dv = vec3(a.z, a.w, b) * fade;
    float Dd, DTd;
    vec3 Dvd;
    sampleDiskGas(p, Dd, DTd, Dvd);
    // The two gases radiate in proportion to their mass: the temperature of the mixture is the emission-weighted one
    // (mean of T^4), so a cold clump in front of the hot disk dims it by its share only
    float D3 = D;
    float T3 = D3 > 1e-30 ? DT / D3 : 0.0;
    float Td = Dd > 1e-30 ? DTd / Dd : 0.0;
    D = D3 + Dd;
    Dv += Dvd;
    if (D > 1e-30) {
        float t3 = T3 * T3;
        float td = Td * Td;
        float mean4 = (D3 * t3 * t3 + Dd * td * td) / D;
        DT = D * sqrt(sqrt(mean4));
    } else {
        DT = 0.0;
    }
    return true;
}

// Fixed per-pixel offset in [0, 1) of the sample inside a march step (interleaved gradient noise); 0.5 without jitter
float particleJitter() {
    if (u_particleSmooth.z < 0.5) return 0.5;
    return fract(52.9829189 * fract(dot(vec2(g_pixel), vec2(0.06711056, 0.00583715))));
}

// Absorption per r_g [1/r_g] at the intersection of a straight ray with the equatorial plane of the particle hole;
// -1 when there is no such point or no grid (debug view 5).
float particleSliceDepth(vec3 rayOrigin, vec3 rayDir) {
    int hole = int(u_particleInfo.w);
    if (hole < 0) return -1.0;
    vec3 axis = normalize(u_blackHoles[hole].spinAxis);
    float facing = dot(rayDir, axis);
    if (abs(facing) < 1e-6) return -1.0;
    float t = dot(u_blackHoles[hole].position - rayOrigin, axis) / facing;
    if (t <= 0.0) return -1.0;
    vec3 rel = (rayOrigin + rayDir * t - u_blackHoles[hole].position) / u_blackHoles[hole].mass;
    float D, DT;
    vec3 Dv;
    if (!sampleParticleGrid(rel, D, DT, Dv)) return -1.0;
    return D * u_particleInfo.y;
}
#endif

#endif
