uniform int u_accretionDiskVolumetric = 0;
uniform float u_accDiskHeight = 0.2;
uniform float u_accDiskNoiseScale = 1.0;
uniform float u_accDiskNoiseLOD = 5.0;
uniform float u_accDiskSpeed = 0.5;
uniform int u_gravitationalRedshiftEnabled = 1;
uniform float u_dopplerBeamingEnabled = 1.0;
uniform float u_time;

// ------------------------------------------------------------------------------------------------------------
// Section Disk Colour
// ------------------------------------------------------------------------------------------------------------
// Returns the optical depth (density) at this position for volumetric rendering
float adiskColor(vec4 posSph, inout vec3 color, inout float alpha, float eventHorizonRadius, vec3 rayOrigin, float blackHoleMass) {
    float iscoRadius = 2.4f * eventHorizonRadius;
    float outerRadius = 6.7f * eventHorizonRadius;
    float r_sph = posSph.y;
    float theta_sph = posSph.z;
    float phi_sph = posSph.w;

    if (r_sph < iscoRadius || r_sph > outerRadius) return 0.0;

    // Base density calculation
    float density;

    if (u_accretionDiskVolumetric == 1) {
        // Volumetric density using FBM
        // Normalize position to disk space
        vec3 diskPos = toCartesian(posSph.yzw) / outerRadius;

        // Add rotation animation
        float animatedTheta = phi_sph + u_time * u_accDiskSpeed;
        float r_cyl = length(diskPos.xz);
        vec3 animatedPos = vec3(
            r_cyl * cos(animatedTheta),
            0,
            r_cyl * sin(animatedTheta)
        ) * u_accDiskNoiseScale * 2.0;

        // Initial SDF distance field
        float d = abs(diskPos.y / u_accDiskHeight) - 0.5;

        // Apply FBM to get volumetric density
        float sdf = sdFbm(animatedPos, d);

        // Convert SDF to density (negative = inside volume)
        density = max(0.0, -sdf);
        density *= 0.3;
    } else {
        vec3 posCart = toCartesian(posSph.yzw);
        density = max(0.0, 1.0 - length(posCart / vec3(outerRadius, u_accDiskHeight, outerRadius)));
    }

    if (density < EPSILON) return 0.0;

    float r_cyl = r_sph * sin(theta_sph);

    float noise = 1.0;

    // Apply additional noise layers if not using volumetric (to maintain compatibility)
    if (u_accretionDiskVolumetric == 0) {
        for (int i = 0; i < int(u_accDiskNoiseLOD); i++) {
            float animatedTheta = phi_sph;
            if (i % 2 == 0) {
                animatedTheta += u_time * u_accDiskSpeed;
            } else {
                animatedTheta -= u_time * u_accDiskSpeed;
            }

            vec3 noiseCoord = vec3(
            r_cyl * cos(animatedTheta),
            r_sph * cos(theta_sph),
            r_cyl * sin(animatedTheta)
            ) * pow(max(1, i), 2) * u_accDiskNoiseScale;

            noise *= 0.5 * worley(noiseCoord, 1.0f) + 0.3;
        }
    } else {
        // For volumetric, add subtle detail noise
        vec3 detailCoord = toCartesian(posSph.yzw) * u_accDiskNoiseScale * 5.0;
        noise = 0.7 + 0.3 * worley(detailCoord, 5.0f);
    }

    vec3 posCart = toCartesian(posSph.yzw);

    float grFactor = 1.0;
    if (u_gravitationalRedshiftEnabled == 1) {
        grFactor = calculateRedShift(posCart / max(1e-6, eventHorizonRadius));
        grFactor = max(grFactor, 1e-6);
    }

    float doppler = 1.0;
    if (u_dopplerBeamingEnabled > 0.5) {
        vec3 viewDir = normalize(rayOrigin - (posCart + u_cameraPos));
        doppler = calculateDopplerEffect(posCart / max(1e-6, eventHorizonRadius), viewDir);
    }

    float dopplerRedshift = 1.0 / mix(1.0, doppler * grFactor, float(u_dopplerBeamingEnabled > 0.5));

    float temp = getAccretionDiskTemperature(blackHoleMass, r_sph, iscoRadius);
    vec3 bbColor = getBlackbodyColorLUT(temp, dopplerRedshift);

    float beamingFactor = 10.0;
    if (u_dopplerBeamingEnabled > 0.5) {
        beamingFactor = pow(max(0.1, doppler), 3.0);
    }

    bbColor = vec3(1.0, 0.5, 0.2);
    bbColor = pow(bbColor, vec3(1.0 / 2.2));

    // Increase contrast to make darker parts darker and brighter parts brighter
    float contrastNoise = pow(noise, 3.0) * 3.5;

    vec3 emission = bbColor * contrastNoise * beamingFactor;
    color += emission * alpha * 0.9;
    return density * 2.0;
}