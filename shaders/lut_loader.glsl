vec3 getBlackbodyColorLUT(float temperature, float redshiftFactor) {
    float tempRange = u_lutTempMax - u_lutTempMin;
    float tempNorm = (temperature - u_lutTempMin) / tempRange;
    tempNorm = max(0.0f, min(1.0f, tempNorm));

    float redshiftRange = u_lutRedshiftMax - u_lutRedshiftMin;
    float redshiftNorm = (redshiftFactor - u_lutRedshiftMin) / redshiftRange;
    redshiftNorm = max(0.0f, min(1.0f, redshiftNorm));

    vec3 color = texture(u_blackbodyLUT, vec2(tempNorm, redshiftNorm)).rgb;
    return color;
}

// Binet term of the Schwarzschild null orbit, d2u/dphi2 + u = 3 M u^2 (u = 1/r), as a central force
// -3 M h^2 / r^4 on a unit-speed ray with h = |r x d|. Its photon sphere is at 3 M and its critical impact
// parameter is exactly 3 sqrt(3) M.
vec3 calculateAcceleration(float angMomentumSqrd, vec3 relPos, float mass) {
    float rSqrd = max(dot(relPos, relPos), 1e-6);
    return (-3.0 * mass * angMomentumSqrd / (rSqrd * rSqrd * sqrt(rSqrd))) * relPos;
}

const float HR_MASS_MIN = 0.08;
const float HR_MASS_MAX = 100.0;
const float HR_LOG_MASS_MIN = log(HR_MASS_MIN);
const float HR_LOG_MASS_MAX = log(HR_MASS_MAX);

float getTemperatureFromMass(float mass) {
    float logMass = log(clamp(mass, HR_MASS_MIN, HR_MASS_MAX));
    float massNorm = (logMass - HR_LOG_MASS_MIN) / (HR_LOG_MASS_MAX - HR_LOG_MASS_MIN);
    massNorm = clamp(massNorm, 0.0, 1.0);

    vec3 hrData = texture(u_hrDiagramLUT, vec2(massNorm, 0.5)).rgb;
    return hrData.r;
}
