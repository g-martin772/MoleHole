float adiskColorVariant(vec4 posSph, inout vec3 color, inout float alpha, float eventHorizonRadius, vec3 rayOrigin, float blackHoleMass, vec3 blackHoleSpinAxis) {
    float iscoRadius = 2.4f * eventHorizonRadius;
    float outerRadius = 6.7f * eventHorizonRadius;
    float r_sph = posSph.y;
    float theta_sph = posSph.z;
    float phi_sph = posSph.w;
    float density = 1.0f;
    vec3 bbColor;
    float noise = 1.0f;
    if (r_sph < iscoRadius || r_sph > outerRadius) return 0.0;

    bbColor = vec3(1.0f, 0.5f, 0.2f);
    /*bbColor = pow(bbColor, vec3(1.0 / 2.2));
    float r_cyl = r_sph * sin(theta_sph);
    vec3 noiseCoord = vec3(
    r_cyl * cos(phi_sph),
    r_sph * cos(theta_sph),
    r_cyl * sin(phi_sph)
    ) * pow(max(1, i), 2) * u_accDiskNoiseScale;

    noise *= 0.5 * worley(noiseCoord, 1.0f) + 0.3;*/

    vec3 diskPos = toCartesian(posSph.yzw) / outerRadius;

    float spinPlaneDistance = dot(diskPos, normalize(blackHoleSpinAxis));
    float d = abs(spinPlaneDistance) - 0.4f;
    float falloff = exp(-100.0f * abs(spinPlaneDistance));;
    density = worley(diskPos, u_accDiskNoiseScale) * max(0.0f, -sdFbm(diskPos, d)) * falloff;

    color += 0.001f * bbColor;
    return density;
}


// Returns the optical depth (density) at this position for volumetric rendering
float adiskColor(vec4 posSph, inout vec3 color, inout float alpha, float eventHorizonRadius, vec3 rayOrigin, float blackHoleMass, vec3 blackHoleSpinAxis) {
    float iscoRadius = 2.4f * eventHorizonRadius;
    float outerRadius = 100.0f * eventHorizonRadius;
    float r_sph = posSph.y;
    float theta_sph = posSph.z;
    float phi_sph = posSph.w;

    if (r_sph < iscoRadius || r_sph > outerRadius) return 0.0;

    float density;

    if (u_accretionDiskVolumetric == 1) {
        // Volumetric density using FBM
        // Normalize position to disk space
        vec3 diskPos = toCartesian(posSph.yzw);

        // rotation animation
        float animatedTheta = phi_sph + u_time * u_accDiskSpeed;
        float r_cyl = length(diskPos.xz);
        vec3 animatedPos = vec3(
            r_cyl * cos(animatedTheta),
            0,
            r_cyl * sin(animatedTheta)
        ) * u_accDiskNoiseScale * 2.0;

        // SDF
        float spinPlaneDistance = dot(diskPos, normalize(blackHoleSpinAxis));
        float d = abs(spinPlaneDistance) - 0.5f * u_accDiskHeight;

        // FBM
        float sdf = sdFbm(animatedPos, d);

        // final density
        density = max(0.0, -sdf);
    } else {
        vec3 posCart = toCartesian(posSph.yzw);
        density = max(0.0, 1.0 - length(posCart / vec3(outerRadius, u_accDiskHeight, outerRadius)));
    }

    if (density < EPSILON) return 0.0;

    float r_cyl = r_sph * sin(theta_sph);

    float noise = 1.0;

    // additional noise layers if not using volumetric
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
        vec3 detailCoord = toCartesian(posSph.yzw) * u_accDiskNoiseScale;
        noise = worley(detailCoord, 5.0f);
    }

    vec3 posCart = toCartesian(posSph.yzw);

    float grFactor = 1.0;
    if (u_gravitationalRedshiftEnabled == 1) {
        grFactor = calculateRedShift(posCart / max(1e-6, eventHorizonRadius));
        grFactor = max(grFactor, 1e-6);
    }

    vec3 bbColor;
    float doppler = 1.0;
    if (u_dopplerBeamingEnabled > 0.5) {
        vec3 viewDir = normalize(rayOrigin - (posCart + u_cameraPos));
        doppler = calculateDopplerEffect(posCart / max(1e-6, eventHorizonRadius), viewDir);

        float dopplerRedshift = 1.0 / mix(1.0, 2.0f * doppler * grFactor, float(u_dopplerBeamingEnabled > 0.5));
        float temp = getAccretionDiskTemperature(blackHoleMass, r_sph, iscoRadius);
        bbColor = getBlackbodyColorLUT(temp, dopplerRedshift);
    }
    else {
        float dopplerRedshift = 0.5f;
        float temp = getAccretionDiskTemperature(blackHoleMass, r_sph, iscoRadius);
        bbColor = getBlackbodyColorLUT(temp, dopplerRedshift);
    }

    // Increase contrast to make darker parts darker and brighter parts brighter
    float contrastNoise = pow(noise, 3.0) * 3.5;

    vec3 emission = 0.1f * bbColor * contrastNoise;
    color += emission * alpha;
    return density;
}
