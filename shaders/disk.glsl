// Accretion disk shading shared by the analytic-lens and the integrated tracer.
// Physics, fixed by the metric and the host: inner edge at the ISCO, Page–Thorne temperature profile
// (u_diskProfile), Keplerian angular velocity Omega(r) of the active metric and the frequency ratio g, which the
// caller computes. Look, left open by the physics: turbulent structure, hot spots, height, opacity and the
// display temperature. Background and sources: docs/notes/disk_rendering.md

// Emitted temperature [K] at radius r (render units) from the host's Page–Thorne table; 0 outside the disk.
float diskTemperature(int hole, float r) {
    vec4 info = u_diskInfo[hole];
    if (r < info.y || r > info.z || info.x <= 0.0) return 0.0;
    float s = log(r / info.y) / log(info.z / info.y) * 63.0;
    int i = clamp(int(floor(s)), 0, 62);
    float f = s - float(i);
    float a = u_diskProfile[hole * 16 + i / 4][i % 4];
    float b = u_diskProfile[hole * 16 + (i + 1) / 4][(i + 1) % 4];
    return mix(a, b, f) * info.x;
}

// Disk clock in render units: u_accDiskSpeed is the angular speed of the inner edge in rad/s of wall time.
float diskTime(Spacetime st) {
    return u_time * u_accDiskSpeed / max(float(keplerOmega(st, st.rIsco)), 1e-6);
}

float diskFbm(vec3 p, int octaves) {
    float sum = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < octaves; i++) {
        sum += amplitude * snoise(p);
        p = p * 2.03 + vec3(17.1, 9.2, 3.7);
        amplitude *= 0.5;
    }
    return sum;
}

// Turbulent structure carried with the gas. The texture coordinate is phi - Omega(r) * age, so inner rings
// overtake outer ones (Keplerian shear). Pure shear would wind the texture up, so two layers half a period
// apart are crossfaded and each is reset while invisible (flow-map technique); the period is a quarter orbit,
// which bounds the shear to about 2.4 Delta r / r. Noise is sampled in (log r, angle, z / H), elongated along phi.
// Returns a log-normal modulation of the dissipated flux with mean about 1.
float diskPattern(Spacetime st, float r, float phi, float zOverH, float t) {
    float omega = max(float(keplerOmega(st, real(r))), 1e-6);
    float period = 0.5 * PHYS_PI / omega;
    int octaves = clamp(int(u_accDiskNoiseLOD + 0.5), 1, 5);
    float scale = max(u_accDiskNoiseScale, 0.05);
    float logR = log(r / float(st.rIsco));

    float value = 0.0;
    float weights = 0.0;
    for (int layer = 0; layer < 2; layer++) {
        float shifted = t + float(layer) * 0.5 * period;
        float cycle = floor(shifted / period);
        float age = shifted - cycle * period;
        float weight = 1.0 - abs(2.0 * age / period - 1.0);
        float angle = phi - omega * age;
        vec3 offset = hash33(vec3(cycle, float(layer), 7.0)) * 100.0;
        vec3 q = vec3(0.9 * scale * cos(angle), 0.9 * scale * sin(angle), 3.0 * scale * logR + 0.5 * zOverH);
        value += weight * diskFbm(q + offset, octaves);
        weights += weight * weight;
    }
    value /= sqrt(max(weights, 1e-4)); // variance-preserving blend: no contrast dip half way
    return exp(1.6 * value - 0.3);
}

// Hotter clumps of gas on circular geodesics at Omega(r); returns the sum of their Gaussian profiles.
float diskHotSpots(Spacetime st, float r, float phi, float t) {
    float rIn = float(st.rIsco);
    float boost = 0.0;
    for (int k = 0; k < u_diskHotSpots; k++) {
        vec3 h = 0.5 + 0.5 * hash33(vec3(float(k), 3.0, 11.0));
        float rk = rIn * (1.15 + 1.6 * h.x);
        float phik = 2.0 * PHYS_PI * h.y + float(keplerOmega(st, real(rk))) * t;
        float dphi = mod(phi - phik + PHYS_PI, 2.0 * PHYS_PI) - PHYS_PI;
        float size = (0.3 + 0.3 * h.z) * float(st.M) * rk / rIn;
        vec2 d = vec2(r - rk, rk * dphi) / size;
        boost += exp(-0.5 * dot(d, d));
    }
    return boost;
}

// Surface density goes to zero at the zero-torque inner edge and fades out towards the (artistic) outer edge.
float diskEdges(int hole, float r) {
    vec4 info = u_diskInfo[hole];
    return smoothstep(info.y, info.y * 1.15, r) * (1.0 - smoothstep(0.75 * info.z, info.z, r));
}

// Local gas state at (r, phi, z / H): emitted temperature and the column-density factor (turbulence and edges).
// Turbulence modulates the dissipated flux F, so T scales with its fourth root; hot spots heat the gas.
void diskGas(Spacetime st, int hole, float r, float phi, float zOverH, out float temperature, out float column) {
    float t = diskTime(st);
    float pattern = diskPattern(st, r, phi, zOverH, t);
    float spots = diskHotSpots(st, r, phi, t);
    temperature = diskTemperature(hole, r) * pow(pattern, 0.25) * (1.0 + 0.35 * spots);
    column = pattern * diskEdges(hole, r);
}

// Luminance of the hottest unshifted gas: one global calibration (like a fixed exposure) that puts the disk
// near the brightness of the pre-HDR renderer, so relative brightness and beaming stay physical.
const float DISK_REFERENCE_LUMINANCE = 0.25;

// Observed radiance of thermal gas at emitted temperature T seen with frequency ratio g. Colour from the blackbody
// LUT at the observed temperature g T; brightness from I ∝ T_obs^4 (I_nu / nu^3 is invariant along a ray).
// The display temperature rescales every temperature by one factor for the colour lookup only, so ratios and
// shifts stay physical.
vec3 diskSourceRadiance(int hole, float temperature, float g) {
    float peak = max(u_diskInfo[hole].x, 1e-6);
    float displayScale = u_diskDisplayTemperature > 0.0 ? u_diskDisplayTemperature / peak : 1.0;
    vec3 lut = getBlackbodyColorLUT(g * temperature * displayScale, 1.0);
    float luminance = dot(lut, vec3(0.2126, 0.7152, 0.0722));
    vec3 chroma = luminance > 1e-6 ? lut / luminance : vec3(0.0);
    float ratio = g * temperature / peak;
    return DISK_REFERENCE_LUMINANCE * chroma * (ratio * ratio) * (ratio * ratio);
}

// Flared volume: scale height H(r) = u_accDiskHeight r / 10 (u_accDiskHeight is H in M at r = 10 M), so the
// aspect ratio H / r is the same at every radius and does not depend on where the ISCO lies.
float diskScaleHeight(Spacetime st, float r) {
    return max(u_accDiskHeight, 0.005) * r / 10.0;
}

// Absorption coefficient of the Gaussian vertical profile, normalised so the optical depth straight through the
// disk is u_diskOpacity times the column factor.
float diskVolumeAbsorption(float column, float zOverH, float H) {
    return u_diskOpacity * column * exp(-0.5 * zOverH * zOverH) / (2.5066283 * H);
}

// Thin disk: optical depth of the sheet for a ray crossing it at |cos| mu to the normal.
float diskSheetDepth(float column, float mu) {
    return u_diskOpacity * column / max(mu, 0.05);
}

// Frequency ratio g for the analytic-lens tracer, which has no geodesic momentum: the photon's angular momentum
// about the spin axis is taken from its local straight-line direction (E = 1, observer at rest far away), then
// the exact circular-orbit formula of the metric applies (diskRedshift), frame dragging included.
float analyticLensDiskG(Spacetime st, vec3 relativePos, vec3 rayDirection, vec3 spinAxis) {
    // The camera ray runs against the light, so the photon travels along -rayDirection.
    float L = dot(cross(relativePos, -rayDirection), normalize(spinAxis));
    float gTotal, alphaEmitter;
    diskRedshift(st, real(length(relativePos)), real(R_PI) * real(0.5), real(1.0), real(L), gTotal, alphaEmitter);
    return (u_gravitationalRedshiftEnabled == 1 ? alphaEmitter : 1.0)
         * (u_dopplerBeamingEnabled > 0.5 ? gTotal / max(alphaEmitter, 1e-6) : 1.0);
}

// Radiative transfer through a segment of optical depth dTau with source S: I += T S (1 - e^-dTau), T *= e^-dTau.
// Exact for a constant source over the segment, so the result does not depend on the step size.
void diskAccumulate(float dTau, vec3 source, inout vec3 color, inout float alpha) {
    float transmittance = exp(-dTau);
    color += alpha * source * (1.0 - transmittance);
    alpha *= transmittance;
}