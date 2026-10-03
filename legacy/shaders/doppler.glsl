// ------------------------------------------------------------------------------------------------------------
// Section Doppler Effect
// ------------------------------------------------------------------------------------------------------------
float calculateDopplerEffect(vec3 pos, vec3 viewDir)
{
    vec3 vel;
    float r = sqrt(dot(pos, pos));
    float safeR = max(r, 1.0 + 1e-6);

    if (r < 1.0) return 1.0;

    float velMag0 = -sqrt((1.0 * 1.0 / (2.0 * (safeR - 1.0))));
    float velMag1 = -sqrt((1.0 * 1.0 / r) * (1.0 - 3.0 * 1.0 / (safeR)));
    float velMag = velMag0;
    vec3 velDir = normalize(cross(vec3(0.0, 1.0, 0.0), pos));
    vel = velDir * velMag;
    vec3 beta_s = vel;

    float gamma = inversesqrt(max(1e-12, 1.0 - dot(beta_s, beta_s)));
    float dopplerShift = gamma * (1.0 + dot(vel, normalize(viewDir)));

    return max(0.0001, dopplerShift);
}

// ------------------------------------------------------------------------------------------------------------
// Section Redshift
// ------------------------------------------------------------------------------------------------------------
float calculateRedShift(vec3 pos)
{
    float dist = sqrt(dot(pos, pos));
    float valid = step(1.0, dist);

    float redshift = sqrt(max(0.0, 1.0 - 1.0 / max(dist, 1.0))) - 1.0;
    redshift = 1.0 / (1.0 + redshift);

    redshift *= valid;
    return redshift;
}