struct HitRecord {
    bool hit;
    float t;
    vec3 color;
    int type; // 0 = none, 1 = sphere, 2 = mesh, 3 = influence boundary
    int objectIndex;
};

bool isInInfluenceZone(vec3 pos, out int closestBHIndex, out float distanceToBH, out float influenceRadius) {
    closestBHIndex = -1;
    distanceToBH = 1e10;
    influenceRadius = 0.0;

    if (u_renderBlackHoles == 0) return false;

    for (int j = 0; j < u_numBlackHoles; j++) {
        vec3 relativePos = pos - u_blackHoles[j].position;
        float dist = length(relativePos);
        float r_s = calculateEventHorizonRadius(u_blackHoles[j].mass);
        float r_i = calculateInfluenceRadius(r_s);

        if (dist < r_i && dist < distanceToBH) {
            closestBHIndex = j;
            distanceToBH = dist;
            influenceRadius = r_i;
        }
    }
    return closestBHIndex >= 0;
}

HitRecord rayTraceNormalSpace(vec3 rayOrigin, vec3 rayDir, float maxDistance) {
    HitRecord record;
    record.hit = false;
    record.t = maxDistance;
    record.type = 0;

    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));

    if (u_renderSpheres == 1) {
        for (int i = 0; i < u_numSpheres; i++) {
            float t;
            if (intersectSphere(rayOrigin, rayDir, u_spheres[i].position, u_spheres[i].radius, t)) {
                if (t < record.t) {
                    record.hit = true;
                    record.t = t;
                    record.type = 1;
                    record.objectIndex = i;
                    vec3 hitPoint = rayOrigin + rayDir * t;
                    record.color = renderSphere(hitPoint, rayDir, i, lightDir);
                }
            }
        }
    }

    if (u_enableThirdPerson == 1) {
        float t;
        int axis = intersectCrosshair(rayOrigin, rayDir, u_cameraPos, u_cubeSize * 0.3, t);
        if (axis > 0) {
            if (t < record.t) {
                record.hit = true;
                record.t = t;
                record.type = 3;
                record.objectIndex = 0;

                vec3 hitPoint = rayOrigin + rayDir * t;
                vec3 normal = getCrosshairNormal(hitPoint, u_cameraPos, axis);
                vec3 crosshairColor = getCrosshairColor(axis);
                record.color = crosshairColor;
            }
        }
    }
    return record;
}

vec3 rayMarchInfluenceZone(int closestHole, vec3 rayOrigin, vec3 rayDirection, out bool hitEventHorizon, out bool exitedZone, out vec3 newOrigin, out vec3 newDirection, out vec3 hitPoint, out bool hitSolid) {
    hitEventHorizon = false;
    exitedZone = false;
    hitSolid = false;
    hitPoint = vec3(0.0);
    vec3 color = vec3(0.0);
    float alpha = 1.0;

    newOrigin = rayOrigin;
    newDirection = rayDirection;

    float stepSize = u_rayStepSize * 10.0f;
    float maxSteps = u_maxRaySteps / 50;
    float adaptiveStepRate = u_adaptiveStepRate;
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));

    float mass = u_blackHoles[closestHole].mass;
    /*stepSize *= 1.0f * mass;
    maxSteps *= 1.0f * mass;
    adaptiveStepRate *= 1.0f * mass;*/

    // volumetric
    float visibility = 1.0f;
    float volumeDepth = 0.0f;

    for (int i = 0; i < maxSteps; i++) {
        int closestBH;
        float distToBH;
        float influenceR;
        bool inZone = isInInfluenceZone(newOrigin, closestBH, distToBH, influenceR);

        if (!inZone) {
            exitedZone = true;
            return color;
        }

        vec3 relativePos = newOrigin - u_blackHoles[closestBH].position;

        // Calculate orbital angular momentum
        vec3 orbitalAngMomentum = cross(relativePos, newDirection);
        vec3 bhAngMomentum = calculateAngularMomentumFromSpin(
            u_blackHoles[closestBH].spin,
            u_blackHoles[closestBH].spinAxis,
            u_blackHoles[closestBH].mass
        );
        vec3 totalAngMomentum = orbitalAngMomentum + bhAngMomentum * 0.1;
        float angMomSqrd = dot(totalAngMomentum, totalAngMomentum);

        float r_s = calculateEventHorizonRadius(u_blackHoles[closestBH].mass);

        // Adaptive step size
        float currentStepSize = stepSize * min(adaptiveStepRate, distToBH / r_s);

        if (u_gravitationalLensingEnabled == 1) {
            vec3 acceleration = calculateAccelerationLUT(angMomSqrd, relativePos);
            newDirection += acceleration * currentStepSize;
            newDirection = normalize(newDirection);
        }

        if (u_accretionDiskEnabled == 1) {
            // Get optical depth from the accretion disk at this position
            float opticalDepth = adiskColor(vec4(0.0, toSpherical(relativePos)), color, alpha, r_s, newOrigin, u_blackHoles[closestBH].mass);

            // Apply volumetric absorption using Beer-Lambert law
            if (opticalDepth > 0.0) {
                float transmittance = beerLambert(opticalDepth, currentStepSize);
                alpha *= transmittance;
                if (alpha < 0.01) {
                    return color;
                }
            }
        }

        // Check object intersections within marching step
        HitRecord hit = rayTraceNormalSpace(newOrigin, newDirection, currentStepSize);
        if (hit.hit) {
            if (hit.type == 1) {
                hitSolid = true;
                hitPoint = newOrigin + newDirection * hit.t;
            }
            return color + hit.color;
        }

        // Check event horizon
        if (distToBH < r_s) {
            hitEventHorizon = true;
            return color;
        }
        newOrigin += newDirection * currentStepSize;
    }
    return color;
}

vec3 hybridRayTrace(vec3 rayOrigin, vec3 rayDirection, out vec3 hitPoint, out bool hitSolid) {
    vec3 color = vec3(0.0);
    vec3 currentOrigin = rayOrigin;
    vec3 currentDir = rayDirection;
    hitSolid = false;
    hitPoint = vec3(0.0);

    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    int maxOuterIterations = max(50, u_maxRaySteps / 200);

    for (int iter = 0; iter < maxOuterIterations; iter++) {
        int closestBH;
        float distToBH;
        float influenceR;
        bool inZone = isInInfluenceZone(currentOrigin, closestBH, distToBH, influenceR);

        if (inZone) {
            bool hitHorizon, exited;
            vec3 newOrigin, newDir;
            vec3 zoneHitPoint; bool zoneHitSolid;
            vec3 marchColor = rayMarchInfluenceZone(closestBH, currentOrigin, currentDir, hitHorizon, exited, newOrigin, newDir, zoneHitPoint, zoneHitSolid);
            color += marchColor;
            if (zoneHitSolid) {
                hitSolid = true;
                hitPoint = zoneHitPoint;
            }

            if (hitHorizon) {
                return color;
            }

            if (exited) {
                currentOrigin = newOrigin;
                currentDir = newDir;
                continue;
            }

            return color;
        } else {
            float minDistToInfluence = 1e10;

            if (u_renderBlackHoles == 1) {
                for (int j = 0; j < u_numBlackHoles; j++) {
                    vec3 toCenter = u_blackHoles[j].position - currentOrigin;
                    float distToCenter = length(toCenter);
                    float r_s = calculateEventHorizonRadius(u_blackHoles[j].mass);
                    float r_i = calculateInfluenceRadius(r_s);

                    float distToInfluence = abs(distToCenter - r_i);
                    minDistToInfluence = min(minDistToInfluence, distToInfluence);
                }
            }

            HitRecord hit = rayTraceNormalSpace(currentOrigin, currentDir, 1e10);

            if (hit.hit) {
                if (hit.t < minDistToInfluence) {
                    color += hit.color;
                    if (hit.type == 1) {
                        hitSolid = true;
                        hitPoint = currentOrigin + currentDir * hit.t;
                    }
                    return color;
                } else {
                    currentOrigin += currentDir * (minDistToInfluence + EPSILON);
                    continue;
                }
            }

            if (minDistToInfluence < 1e9) {
                currentOrigin += currentDir * (minDistToInfluence + EPSILON);
                continue;
            }

            color += texture(u_skyboxTexture, directionToSpherical(currentDir)).rgb;
            return color;
        }
    }

    color += texture(u_skyboxTexture, directionToSpherical(currentDir)).rgb;
    return color;
}

// Helper to normalize 4-velocity for light-like geodesics (g_uv v^u v^v = 0)
// For Schwarzschild: -(1-rs/r)*v_t^2 + (1-rs/r)^-1*v_r^2 + r^2*(v_theta^2 + sin^2(theta)*v_phi^2) = 0
vec4 normalize4Velocity(vec4 pos, vec4 vel, float M) {
    float r = pos.y;
    float theta = pos.z;
    float rs = 2.0f * G * M / (c * c);

    float r_s_factor = 1.0f - rs / r;
    if (r_s_factor < 0.001f) r_s_factor = 0.001f;  // Avoid singularity

    // For light rays: g_uv v^u v^v = 0
    // Compute spatial magnitude and adjust temporal component
    float v_spatial_sq = (vel.y * vel.y) / r_s_factor +
    vel.z * vel.z * r * r +
    vel.w * vel.w * r * r * sin(theta) * sin(theta);

    // Adjust temporal component to maintain null geodesic
    vel.x = sqrt(max(v_spatial_sq / r_s_factor, 0.0f));

    return vel;
}

vec3 rk4RayMarching(vec3 rayOrigin, vec3 rayDirection) {

    vec3 colorValue = vec3(0.0f);
    float alpha = 1.0f;

    if (u_blackHoles[0].mass == 0.0f)
    return texture(u_skyboxTexture, directionToSpherical(rayDirection)).rgb;

    // ray position and direction (Cartesian)
    vec3 pos = rayOrigin;
    vec3 dir = normalize(rayDirection);

    // loop variables
    float stepSize = u_rayStepSize;
    float maxSteps = u_maxRaySteps;
    float adaptiveStepRate = u_adaptiveStepRate;

    // position relative to black hole
    vec3 relativePosCart = pos - u_blackHoles[0].position;

    // convert to spherical coordinates
    vec4 relativePosSph = vec4(0.0f, toSpherical(relativePosCart));
    vec4 relativeDirSph = vec4(1.0f, vel_cartesian_to_spherical(relativePosCart, dir));

    // compute event horizon radius
    float r_s = calculateEventHorizonRadius(u_blackHoles[0].mass);

    // main loop
    for (int i = 0; i < maxSteps; i++) {

        float dist = relativePosSph.y;

        if (u_accretionDiskEnabled == 1) {
            float dAlpha = adiskColorVariant(relativePosSph, colorValue, alpha, r_s, rayOrigin, u_blackHoles[0].mass, u_blackHoles[0].spinAxis);
            alpha *= (1.0f - clamp(dAlpha, 0.0f, 1.0f));
            if (alpha < 0.01f) {
                return colorValue;
            }
        }

        // check if we've hit the event horizon
        if (dist < r_s + 100.0f * EPSILON) {
            return vec3(0.0f);
        }

        // check if we've escaped the influence zone
        if (dist > r_s * 25.0f) {
            break;
        }

        // adaptive step size near horizon
        if (dist < r_s * 3.0f) {
            stepSize = u_rayStepSize * adaptiveStepRate * 0.1f;
        } else if (dist < r_s * 10.0f) {
            stepSize = u_rayStepSize * adaptiveStepRate;
        } else {
            stepSize = u_rayStepSize;
        }

        // calculate specific angular momentum from spin parameter
        float a = u_blackHoles[0].spin * calculateEventHorizonRadius(u_blackHoles[0].mass) / 2.0f;

        // set charge
        float Q = u_blackHoles[0].charge;

        // geodesic integration (RK4)
        relativeDirSph = normalize4Velocity(relativePosSph, relativeDirSph, u_blackHoles[0].mass);
        rk4_step(relativePosSph, relativeDirSph, stepSize, u_blackHoles[0].mass, a, Q);
        relativeDirSph = normalize4Velocity(relativePosSph, relativeDirSph, u_blackHoles[0].mass);
    }

    dir = vel_spherical_to_cartesian(relativePosSph.yzw, relativeDirSph.yzw);

    // map skybox color from final ray direction
    vec3 color = texture(u_skyboxTexture, directionToSpherical(dir)).rgb;
    return color;
}
