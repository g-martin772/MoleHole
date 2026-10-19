#ifndef PARTICLE_DISK_GLSL
#define PARTICLE_DISK_GLSL

// Particle disk in the analytic-lens and integrated tracers: radiative transfer through the grid of
// particle_grid.glsl. Grey gas, j = kappa rho B(T) with the calibration of the analytic disk (disk_radiance.glsl),
// proper length ds = (-p . u) dlambda, g from the photon momentum and the fluid velocity of the grid.
// Notes: docs/notes/particle-disk.md

#include "particle_grid.glsl"

bool particleDiskActive(int hole) {
    return u_accretionDiskEnabled == 1 && int(u_particleInfo.w) == hole;
}

float particleInfluenceRadius(int hole, float base) {
    return particleDiskActive(hole) ? max(base, particleExtent() * u_blackHoles[hole].mass) : base;
}

// Gas at a point relative to the hole [render units, world axes]: D [kg/m^2], emitted temperature [K], velocity [c].
bool particleGas(vec3 relWorld, float mass, out float D, out float T, out vec3 v) {
    float DT;
    vec3 Dv;
    T = 0.0;
    v = vec3(0.0);
    if (!sampleParticleGrid(relWorld / mass, D, DT, Dv) || !(D > 1e-30)) return false;
    T = DT / D * u_particleInfo.z;
    v = Dv / D;
    float speed = length(v);
    if (speed > 0.95) v *= 0.95 / speed;
    return true;
}

// Coordinate velocity dx^i/dt of a fluid element with local Cartesian velocity v (units of c) at Boyer-Lindquist (r, theta, phi)
void particleCoordinateVelocity(real r, real theta, real phi, real a, vec3 v, out real dr, out real dtheta, out real dphi) {
    real s, ct, sp, cp;
    rsincos_pair(theta, s, ct);
    rsincos_pair(phi, sp, cp);
    real rho = sqrt(r * r + a * a);
    rvec3 dR = rvec3(r / rho * s * cp, r / rho * s * sp, ct);
    rvec3 dTheta = rvec3(rho * ct * cp, rho * ct * sp, -r * s);
    rvec3 dPhi = rvec3(-rho * s * sp, rho * s * cp, real(0.0));
    rvec3 vel = rvec3(v);
    dr = dot(vel, dR) / dot(dR, dR);
    dtheta = dot(vel, dTheta) / max(dot(dTheta, dTheta), real(1e-12));
    dphi = dot(vel, dPhi) / max(dot(dPhi, dPhi), real(1e-12));
}

// Frequency ratio g = nu_camera / nu_emitted of gas moving with the grid velocity (hole-local Cartesian axes, units
// of c, read as coordinate velocity dx/dt), seen with the photon momentum (E, L, p_r, p_theta) normalised like
// integratedDiskG. u^t from the metric; the Keplerian disk formula of diskRedshift is the special case
// dr = dtheta = 0, dphi = Omega. gLaunch = 1 / (-p . u) turns an affine step into a proper length in the gas frame.
float particleGasG(Spacetime st, real r, real theta, real phi, vec3 vLocal, real E, real L, real pr, real pth,
                   float cameraEnergy, float cameraAlpha, out float gLaunch) {
    real dr, dtheta, dphi;
    particleCoordinateVelocity(r, theta, phi, st.a, vLocal, dr, dtheta, dphi);
    real gtt, gtphi, gphiphi;
    metricTPhi(st, r, theta, gtt, gtphi, gphiphi);
    real alpha, omegaZamo;
    zamo(st, r, theta, alpha, omegaZamo);
    real s, ct;
    rsincos_pair(theta, s, ct);
    real Sigma = r * r + st.a * st.a * ct * ct;
    real Delta = r * r - real(2.0) * st.M * r + st.a * st.a + st.Q * st.Q;
    real norm = -(gtt + real(2.0) * gtphi * dphi + gphiphi * dphi * dphi + Sigma / max(Delta, real(1e-9)) * dr * dr + Sigma * dtheta * dtheta);
    if (!(norm > real(1e-6))) {
        dr = real(0.0);
        dtheta = real(0.0);
        dphi = omegaZamo;
        norm = -(gtt + real(2.0) * gtphi * dphi + gphiphi * dphi * dphi);
    }
    real ut = inversesqrt(max(norm, real(1e-12)));
    real pu = ut * (E - pr * dr - pth * dtheta - L * dphi);
    gLaunch = float(real(1.0) / max(pu, real(1e-6)));
    float gTotal = gLaunch * cameraEnergy;
    float gGravity = float(alpha) / max(cameraAlpha, 1e-6);
    return (u_gravitationalRedshiftEnabled == 1 ? gGravity : 1.0)
         * (u_dopplerBeamingEnabled > 0.5 ? gTotal / max(gGravity, 1e-6) : 1.0);
}

// Photon of the analytic lens at relMid (hole frame, render units): its local direction is the reverse of the
// camera ray, launched at unit energy from the local ZAMO, as analyticLensDiskG does for the Keplerian disk.
float particleGasGLensed(Spacetime st, mat3 toLocal, vec3 relMid, vec3 rayDirection, vec3 vWorld, out float gLaunch) {
    rvec3 bl = cartesianToBL(rvec3(toLocal * relMid), st.a);
    vec3 er, eTheta, ePhi;
    blBasis(bl.x, bl.y, bl.z, st.a, er, eTheta, ePhi);
    vec3 dirLocal = normalize(toLocal * rayDirection);
    vec3 n = -vec3(dot(dirLocal, er), dot(dirLocal, eTheta), dot(dirLocal, ePhi));
    real E, L, pr, pth;
    launchPhoton(st, bl.x, bl.y, n, E, L, pr, pth);
    return particleGasG(st, bl.x, bl.y, bl.z, toLocal * vWorld, E, L, pr, pth, float(E), 1.0, gLaunch);
}

// Transfer through a path of length dsRender [render units] in gas of column density D and temperature T
void particleAccumulate(float D, float T, float g, float dsRender, float mass, inout vec3 color, inout float alpha) {
    float dTau = u_particleInfo.y * D * dsRender / mass;
    diskAccumulateStable(dTau, diskBlackbodyRadiance(u_particleInfo.z, T, g), color, alpha);
}

#endif
