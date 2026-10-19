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
    vec4 u_particleFrame;  // x: r_g [m], y: fixed-point units per kg (0: float atomics), z: kg per fixed-point unit, w: unused
};

#ifndef PARTICLE_GRID_NO_TEXTURES
// Per level: (D, D T / T_ref, D v_x, D v_y) and (D v_z), D = rho r_g [kg/m^2], v in units of c
layout(set = 0, binding = 14) uniform sampler3D u_particleGrid0A;
layout(set = 0, binding = 15) uniform sampler3D u_particleGrid0B;
layout(set = 0, binding = 16) uniform sampler3D u_particleGrid1A;
layout(set = 0, binding = 17) uniform sampler3D u_particleGrid1B;

float particleExtent() {
    return -u_particleMin[1].x;
}

float particleVoxel(vec3 p) {
    return all(lessThan(abs(p), vec3(-u_particleMin[0].x))) ? u_particleMin[0].w : u_particleMin[1].w;
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
        a = textureLod(u_particleGrid1A, uvwC, 0.0);
        b = textureLod(u_particleGrid1B, uvwC, 0.0).r;
    }
    if (wFine > 0.0) {
        a = mix(a, textureLod(u_particleGrid0A, uvwF, 0.0), wFine);
        b = mix(b, textureLod(u_particleGrid0B, uvwF, 0.0).r, wFine);
    }
    vec3 toEdgeC = min(uvwC, 1.0 - uvwC);
    float fade = smoothstep(0.0, 0.08, min(toEdgeC.x, min(toEdgeC.y, toEdgeC.z)));
    D = a.x * fade;
    DT = a.y * fade;
    Dv = vec3(a.z, a.w, b) * fade;
    return true;
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
