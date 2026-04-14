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
float adiskColor(vec3 pos, inout vec3 color, inout float alpha, float eventHorizonRadius, vec3 rayOrigin, float blackHoleMass) {
    float iscoRadius = 1.8f * eventHorizonRadius;
    float outerRadius = 5.0f * eventHorizonRadius;
    float distanceFromCenter = length(pos);

    if (distanceFromCenter < iscoRadius || distanceFromCenter > outerRadius) return 0.0;

    // Base density calculation
    float density;

    if (u_accretionDiskVolumetric == 1) {
        // Volumetric density using FBM
        // Normalize position to disk space
        vec3 diskPos = pos / outerRadius;

        // Add rotation animation
        float theta = atan(diskPos.z, diskPos.x);
        float animatedTheta = theta + u_time * u_accDiskSpeed;
        float r = length(diskPos.xz);
        vec3 animatedPos = vec3(
            r * cos(animatedTheta),
            0,
            r * sin(animatedTheta)
        ) * u_accDiskNoiseScale * 2.0;

        // Initial SDF distance field
        float d = abs(diskPos.y / u_accDiskHeight) - 0.5;

        // Apply FBM to get volumetric density
        float sdf = sdFbm(animatedPos, d);

        // Convert SDF to density (negative = inside volume)
        density = max(0.0, -sdf);
        density *= 0.3;

        // Apply radial falloff
        //float radialFalloff = smoothstep(outerRadius, iscoRadius * 1.2, distanceFromCenter);
        //density *= radialFalloff;

        // Apply vertical falloff for disk shape
        //float verticalFalloff = exp(-abs(pos.y) / (u_accDiskHeight * outerRadius));
        //density *= verticalFalloff;

    } else {
        density = max(0.0, 1.0 - length(pos.xyz / vec3(outerRadius, u_accDiskHeight, outerRadius)));
    }

    if (density < EPSILON) return 0.0;

    float r = length(pos.xz);
    float theta = atan(pos.z, pos.x);

    float noise = 1.0;

    // Apply additional noise layers if not using volumetric (to maintain compatibility)
    if (u_accretionDiskVolumetric == 0) {
        for (int i = 0; i < int(u_accDiskNoiseLOD); i++) {
            float animatedTheta = theta;
            if (i % 2 == 0) {
                animatedTheta += u_time * u_accDiskSpeed;
            } else {
                animatedTheta -= u_time * u_accDiskSpeed;
            }

            vec3 noiseCoord = vec3(
            r * cos(animatedTheta),
            pos.y,
            r * sin(animatedTheta)
            ) * pow(max(1, i), 2) * u_accDiskNoiseScale;

            noise *= 0.2 * worley(noiseCoord, 3.0f) + 0.5;
        }
    } else {
        // For volumetric, add subtle detail noise
        vec3 detailCoord = pos * u_accDiskNoiseScale * 5.0;
        noise = 0.7 + 0.3 * worley(detailCoord, 5.0f);
    }

    float grFactor = 1.0;
    /*if (u_gravitationalRedshiftEnabled == 1) {
        grFactor = calculateRedShift(pos / max(1e-6, eventHorizonRadius));
        grFactor = max(grFactor, 1e-6);
    }*/

    float doppler = 1.0;
    /*if (u_dopplerBeamingEnabled > 0.5) {
        vec3 viewDir = normalize(rayOrigin - (pos + u_cameraPos));
        doppler = calculateDopplerEffect(pos / max(1e-6, eventHorizonRadius), viewDir);
    }*/

    float dopplerRedshift = 1.0 / mix(1.0, doppler * grFactor, float(u_dopplerBeamingEnabled > 0.5));

    float temp = getAccretionDiskTemperature(blackHoleMass, distanceFromCenter, iscoRadius);
    vec3 bbColor = getBlackbodyColorLUT(temp, dopplerRedshift);

    float beamingFactor = 10.0;
    if (u_dopplerBeamingEnabled > 0.5) {
        beamingFactor = pow(max(0.1, doppler), 3.0);
    }

    vec3 emission = bbColor * noise * density;
    color += emission * alpha * 0.5;
    color = vec3(1.0, 0.5, 0.2);
    return density * 2.0;
}