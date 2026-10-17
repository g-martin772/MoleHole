// Flat-space approximations used by the hybrid and flat tracers; the integrated path computes the exact
// frequency ratio from the metric (diskRedshift in kerr.glsl).
// pos is in units of the horizon radius; the gas orbits prograde around spinAxis.
float calculateDopplerEffect(vec3 pos, vec3 viewDir, vec3 spinAxis)
{
    vec3 vel;
    float r = sqrt(dot(pos, pos));
    float safeR = max(r, 1.0 + 1e-6);

    if (r < 1.0) return 1.0;

    float velMag = sqrt((1.0 * 1.0 / (2.0 * (safeR - 1.0))));
    vec3 velDir = normalize(cross(normalize(spinAxis), pos));
    vel = velDir * velMag;
    vec3 beta_s = vel;

    float gamma = inversesqrt(max(1e-12, 1.0 - dot(beta_s, beta_s)));
    float dopplerShift = gamma * (1.0 + dot(vel, normalize(viewDir)));

    return max(0.0001, dopplerShift);
}

// Orbit around -y, the convention the flat tracer's disk frame was built with.
float calculateDopplerEffect(vec3 pos, vec3 viewDir)
{
    return calculateDopplerEffect(pos, viewDir, vec3(0.0, -1.0, 0.0));
}

float calculateRedShift(vec3 pos)
{
    float dist = sqrt(dot(pos, pos));
    float valid = step(1.0, dist);

    float redshift = sqrt(max(0.0, 1.0 - 1.0 / max(dist, 1.0))) - 1.0;
    redshift = 1.0 / (1.0 + redshift);

    redshift *= valid;
    return redshift;
}

// Blackbody LUT redshift coordinate for the hybrid tracer (0.5 = unshifted baseline the disk was tuned with).
float hybridDiskLutRedshift(vec3 relativePos, vec3 viewDir, float horizonRadius, vec3 spinAxis)
{
    if (u_dopplerBeamingEnabled < 0.5) return 0.5;

    vec3 pos = relativePos / max(1e-6, horizonRadius);
    float grFactor = 1.0;
    if (u_gravitationalRedshiftEnabled == 1) {
        grFactor = max(calculateRedShift(pos), 1e-6);
    }
    float doppler = calculateDopplerEffect(pos, viewDir, spinAxis);
    return 1.0 / (2.0 * doppler * grFactor);
}
