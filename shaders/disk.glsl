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


// The inner edge is the ISCO of the active metric; the outer edge stays at DISK_OUTER_RS (physics.glsl).
// Emission and optical depth of the disk at localPos, given in the hole's spin frame (Z = spin axis, the disk
// lies in the XY plane and orbits towards +phi). lutRedshift is the redshift coordinate of the blackbody LUT
// (temperature is divided by it) and intensity scales the emitted radiance; both come from the caller, so the
// accurate path can feed the exact metric frequency ratio and the hybrid path its flat-space approximation.
// radius is the radial coordinate compared against the disk edges (Boyer–Lindquist r in the accurate path).
// Returns the absorption coefficient for Beer-Lambert.
float adiskColor(vec3 localPos, float radius, float innerRadius, float outerRadius, float blackHoleMass,
                 float lutRedshift, float intensity, inout vec3 color, float alpha) {
    float r_sph = radius;
    if (r_sph < innerRadius || r_sph > outerRadius) return 0.0;

    float r_cyl = length(localPos.xy);
    float height = localPos.z;
    float phi = atan(localPos.y, localPos.x);
    // Disk-aligned coordinates with the height on .y, the layout the noise functions were tuned for.
    vec3 diskPos = vec3(localPos.x, height, localPos.y);

    float density;

    if (u_accretionDiskVolumetric == 1) {
        // rotation animation
        float animatedPhi = phi + u_time * u_accDiskSpeed;
        vec3 animatedPos = vec3(
            r_cyl * cos(animatedPhi),
            0,
            r_cyl * sin(animatedPhi)
        ) * u_accDiskNoiseScale * 2.0;

        // SDF
        float d = abs(height) - 0.5f * u_accDiskHeight;

        // FBM
        float sdf = sdFbm(animatedPos, d);

        // final density
        density = max(0.0, -sdf);
    } else {
        density = max(0.0, 1.0 - length(diskPos / vec3(outerRadius, u_accDiskHeight, outerRadius)));
    }

    if (density < EPSILON) return 0.0;

    float noise = 1.0;

    // additional noise layers if not using volumetric
    if (u_accretionDiskVolumetric == 0) {
        for (int i = 0; i < int(u_accDiskNoiseLOD); i++) {
            float animatedPhi = phi;
            if (i % 2 == 0) {
                animatedPhi += u_time * u_accDiskSpeed;
            } else {
                animatedPhi -= u_time * u_accDiskSpeed;
            }

            vec3 noiseCoord = vec3(
                    r_cyl * cos(animatedPhi),
                    height,
                    r_cyl * sin(animatedPhi)
            ) * pow(max(1, i), 2) * u_accDiskNoiseScale;

            noise *= 0.5 * worley(noiseCoord, 1.0f) + 0.3;
        }
    } else {
        noise = worley(diskPos * u_accDiskNoiseScale, 5.0f);
    }

    float temp = getAccretionDiskTemperature(blackHoleMass, r_sph, innerRadius);
    vec3 bbColor = getBlackbodyColorLUT(temp, lutRedshift);

    // Increase contrast to make darker parts darker and brighter parts brighter
    float contrastNoise = pow(noise, 3.0) * 3.5;

    vec3 emission = 0.05f * bbColor * contrastNoise;
    color += emission * alpha;
    return 10.0f * density;
}
