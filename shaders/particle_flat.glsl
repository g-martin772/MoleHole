#ifndef PARTICLE_FLAT_GLSL
#define PARTICLE_FLAT_GLSL

// Particle disk for the Straight transport: the ray marches the grid in a straight line, flat-space Doppler shift
// 1 / (gamma (1 + d . v)) of the grid velocity (d = camera ray direction, light travels along -d) and the static
// gravitational factor sqrt(1 - 2 M / r). Notes: docs/notes/particle-disk.md

#include "disk_radiance.glsl"
#include "disk_transfer.glsl"
#include "particle_grid.glsl"

// Adds the gas of hole `hole` between the ray origin and tMax; tHalf is set where the transmittance first drops below 1/2
void flatParticleDisk(int hole, vec3 rayOrigin, vec3 rayDir, float tMax, inout vec3 color, inout float transmittance, inout float tHalf) {
    float mass = u_blackHoles[hole].mass;
    vec3 centre = u_blackHoles[hole].position;
    float extent = particleExtent() * mass;
    vec3 inv = 1.0 / (abs(rayDir) + 1e-12) * sign(rayDir + 1e-20);
    vec3 a = (centre - extent - rayOrigin) * inv;
    vec3 b = (centre + extent - rayOrigin) * inv;
    float t0 = max(max(min(a.x, b.x), max(min(a.y, b.y), min(a.z, b.z))), 0.0);
    float t1 = min(min(max(a.x, b.x), min(max(a.y, b.y), max(a.z, b.z))), tMax);
    if (t1 <= t0) return;
    float kappa = u_particleInfo.y;
    float t = t0;
    for (int i = 0; i < 4096 && t < t1; ++i) {
        vec3 rel = rayOrigin + rayDir * t - centre;
        float step = max(0.5 * particleVoxel(rel / mass) * mass, 1e-4 * mass);
        float ds = min(step, t1 - t);
        vec3 mid = rayOrigin + rayDir * (t + particleJitter() * ds) - centre;
        t += ds;
        float D, DT;
        vec3 Dv;
        if (!sampleParticleGrid(mid / mass, D, DT, Dv) || !(D > 1e-30)) continue;
        vec3 v = Dv / D;
        float speed = length(v);
        if (speed > 0.95) v *= 0.95 / speed;
        float T = DT / D * u_particleInfo.z;
        float gamma = inversesqrt(max(1.0 - dot(v, v), 1e-4));
        float doppler = gamma * (1.0 + dot(rayDir, v));
        float r = length(mid) / mass;
        float g = (u_gravitationalRedshiftEnabled == 1 ? sqrt(max(1.0 - 2.0 / r, 0.0)) : 1.0)
                * (u_dopplerBeamingEnabled > 0.5 ? 1.0 / max(doppler, 1e-3) : 1.0);
        float dTau = kappa * D * ds * doppler / mass;
        float before = transmittance;
        diskAccumulateStable(dTau, diskBlackbodyRadiance(u_particleInfo.z, T, g), color, transmittance);
        if (before >= 0.5 && transmittance < 0.5) tHalf = t;
    }
}

#endif
