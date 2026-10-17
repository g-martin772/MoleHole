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

#ifdef GPP_MESH_TRACE
    {
        float t;
        vec3 meshColor;
        if (traceMeshes(rayOrigin, rayDir, record.t, t, meshColor)) {
            record.hit = true;
            record.t = t;
            record.type = 2;
            record.objectIndex = 0;
            record.color = meshColor;
        }
    }
#endif

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
        g_stepCount++;
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
            BlackHoleData bh = u_blackHoles[closestBH];
            vec3 localPos = transpose(spinFrame(bh.spinAxis)) * relativePos;
            float lutRedshift = hybridDiskLutRedshift(relativePos, -newDirection, r_s, bh.spinAxis);
            float opticalDepth = adiskColor(localPos, length(relativePos), bh.iscoRadius, DISK_OUTER_RS * r_s,
                                            bh.mass, lutRedshift, 1.0, color, alpha);

            // Apply volumetric absorption using Beer-Lambert law
            if (opticalDepth > 0.0) {
                float transmittance = beerLambert(opticalDepth, currentStepSize);
                alpha *= transmittance;
                if (alpha < 0.01) {
                    g_termination = TERM_DISK;
                    return color;
                }
            }
        }

        // Check object intersections within marching step
        HitRecord hit = rayTraceNormalSpace(newOrigin, newDirection, currentStepSize);
        if (hit.hit) {
            if (hit.type == 1 || hit.type == 2) {
                hitSolid = true;
                hitPoint = newOrigin + newDirection * hit.t;
            }
            g_termination = hit.type == 3 ? TERM_CROSSHAIR : TERM_OBJECT;
            return color + hit.color;
        }

        // Check event horizon
        if (distToBH < u_blackHoles[closestBH].horizonRadius) {
            hitEventHorizon = true;
            g_termination = TERM_HORIZON;
            return color;
        }
        newOrigin += newDirection * currentStepSize;
    }
    g_termination = TERM_STEPS;
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

    g_maxStepCount = maxOuterIterations + u_maxRaySteps / 50;
    for (int iter = 0; iter < maxOuterIterations; iter++) {
        g_stepCount++;
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
            if (exited) g_termination = TERM_SKY;

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
                    if (hit.type == 1 || hit.type == 2) {
                        hitSolid = true;
                        hitPoint = currentOrigin + currentDir * hit.t;
                    }
                    g_termination = hit.type == 3 ? TERM_CROSSHAIR : TERM_OBJECT;
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

    g_termination = TERM_STEPS;
    color += texture(u_skyboxTexture, directionToSpherical(currentDir)).rgb;
    return color;
}

bool segmentObjectHit(vec3 origin, vec3 dir, float maxDistance, out vec3 color, inout vec3 hitPoint, inout bool hitSolid) {
    HitRecord hit = rayTraceNormalSpace(origin, dir, maxDistance);
    color = vec3(0.0f);
    if (!hit.hit) return false;
    color = hit.color;
    if (hit.type == 1 || hit.type == 2) {
        hitSolid = true;
        hitPoint = origin + dir * hit.t;
    }
    g_termination = hit.type == 3 ? TERM_CROSSHAIR : TERM_OBJECT;
    return true;
}


// Beyond this radius (in M) rays travel straight; 50 M = 25 r_s, where deflection is a few percent.
const float INTEGRATION_RADIUS_M = 50.0f;

// Integrated path: traces the photon that reaches the camera backwards along a null geodesic of the
// Kerr–Newman family. Horizon, ISCO, disk plane, disk motion and redshift all follow from the metric.
// See docs/notes/metric_rendering.md.
vec3 rk4RayMarching(vec3 rayOrigin, vec3 rayDirection, out vec3 hitPoint, out bool hitSolid) {

    vec3 colorValue = vec3(0.0f);
    float alpha = 1.0f;
    hitPoint = vec3(0.0f);
    hitSolid = false;
    bool objects = (u_renderSpheres == 1 && u_numSpheres > 0) || u_numMeshInstances > 0 || u_enableThirdPerson == 1;
    vec3 objectColor;
    vec3 dir = normalize(rayDirection);

    if (u_numBlackHoles == 0 || u_blackHoles[0].mass <= 0.0f) {
        if (objects && segmentObjectHit(rayOrigin, dir, 1e10f, objectColor, hitPoint, hitSolid)) return objectColor;
        return texture(u_skyboxTexture, directionToSpherical(dir)).rgb;
    }

    BlackHoleData bh = u_blackHoles[0];
    Spacetime st = spacetimeOf(bh);
    float mass = bh.mass;
    mat3 frame = spinFrame(bh.spinAxis);
    mat3 toLocal = transpose(frame);

    float influenceRadius = INTEGRATION_RADIUS_M * mass;
    vec3 marchOrigin = rayOrigin;
    {
        vec3 rel = rayOrigin - bh.position;
        if (dot(rel, rel) > influenceRadius * influenceRadius) {
            float enterRadius = influenceRadius * 0.999f;
            float b = dot(rel, dir);
            float disc = b * b - (dot(rel, rel) - enterRadius * enterRadius);
            if (b >= 0.0f || disc < 0.0f) {
                if (objects && segmentObjectHit(rayOrigin, dir, 1e10f, objectColor, hitPoint, hitSolid)) return objectColor;
                return texture(u_skyboxTexture, directionToSpherical(dir)).rgb;
            }
            float entry = -b - sqrt(disc);
            if (objects && segmentObjectHit(rayOrigin, dir, entry, objectColor, hitPoint, hitSolid)) return objectColor;
            marchOrigin = rayOrigin + dir * entry;
        }
    }

    rvec3 cameraBL = cartesianToBL(rvec3(toLocal * (rayOrigin - bh.position)), st.a);
    if (cameraBL.x <= st.rPlus) {
        g_termination = TERM_HORIZON;
        return vec3(0.0f);
    }

    // The photon arriving here travels along -rayDirection; launch it from the local ZAMO frame and integrate
    // backwards in affine parameter. (Reversing the direction instead is wrong once a != 0.)
    rvec3 startBL = cartesianToBL(rvec3(toLocal * (marchOrigin - bh.position)), st.a);
    vec3 dirLocal = normalize(toLocal * dir);
    vec3 er, eTheta, ePhi;
    blBasis(startBL.x, startBL.y, startBL.z, st.a, er, eTheta, ePhi);
    vec3 n = -vec3(dot(dirLocal, er), dot(dirLocal, eTheta), dot(dirLocal, ePhi));
    real E, L, pr, pth;
    launchPhoton(st, startBL.x, startBL.y, n, E, L, pr, pth);

    // Frequencies are measured relative to the launch ZAMO; refer them to the camera's ZAMO instead.
    real cameraAlpha, cameraOmega;
    zamo(st, cameraBL.x, cameraBL.y, cameraAlpha, cameraOmega);
    float cameraEnergy = float((E - cameraOmega * L) / max(cameraAlpha, real(1e-6)));

    rvec4 y = rvec4(startBL.x, startBL.y, pr, pth);
    real phi = startBL.z;

    real rEscape = real(influenceRadius);
    real diskInner = st.rIsco;
    real diskOuter = real(DISK_OUTER_RS * calculateEventHorizonRadius(mass));
    real maxStep = real(10.0f * u_rayStepSize * mass);

    float ergoPath = 0.0f;
    real prevCosTheta = rcos(y.y);

    g_maxStepCount = u_maxRaySteps;
    g_termination = TERM_STEPS;
    for (int i = 0; i < u_maxRaySteps; i++) {
        g_stepCount++;

        real r = y.x;
        if (isnan(r) || r - st.rPlus < real(0.01) * st.M) {
            g_termination = TERM_HORIZON;
            break;
        }
        if (r > rEscape) {
            g_termination = TERM_SKY;
            break;
        }

        rvec4 dy;
        real dphi, K;
        geodesicRhs(st, E, L, y, dy, dphi, K);
        if (u_debugMode == 3) {
            real ct = rcos(y.y);
            real Sigma = r * r + st.a * st.a * ct * ct;
            g_constraintDrift = max(g_constraintDrift, float(abs(K) / (Sigma * max(E * E, real(1e-6)))));
        }

        // Geometric approach to the horizon; capped through the disk so the volume is sampled densely enough.
        real h = real(u_rayStepSize * u_adaptiveStepRate) * (r - st.rPlus);
        if (r < real(1.2) * diskOuter) h = min(h, maxStep);
        // Rays passing close to the polar axis sweep through phi quickly (dphi/dlambda ~ L / sin^2 theta);
        // bound the angle covered per step so they are resolved instead of jumping across the pole.
        real angularRate = abs(dy.y) + abs(dphi);
        h = min(h, real(0.02) / max(angularRate, real(1e-6)));

        vec3 chordStart = objects ? blToCartesian(y.x, y.y, phi, st.a) : vec3(0.0f);
        geodesicStep(st, E, L, y, phi, -h, dy, dphi);
        r = y.x;
        if (isnan(r) || r <= st.rPlus) {
            g_termination = TERM_HORIZON;
            break;
        }

        if (objects) {
            vec3 chord = frame * (blToCartesian(y.x, y.y, phi, st.a) - chordStart);
            float chordLength = length(chord);
            if (chordLength > 0.0f
                && segmentObjectHit(bh.position + frame * chordStart, chord / chordLength, chordLength, objectColor, hitPoint, hitSolid)) {
                return colorValue + alpha * objectColor;
            }
        }

        if (u_debugMode == 4) {
            real ct = rcos(y.y);
            real rErgo = st.M + sqrt(max(st.M * st.M - st.a * st.a * ct * ct - st.Q * st.Q, real(0.0)));
            if (r < rErgo) ergoPath += float(h);
            if (sign(ct) != sign(prevCosTheta) && abs(r - st.rIsco) < real(0.1) * st.M) {
                colorValue += ISCO_RING_COLOR * alpha;
            }
            prevCosTheta = ct;
        }

        if (u_accretionDiskEnabled == 1 && r >= diskInner && r <= diskOuter) {
            vec3 localPos = blToCartesian(r, y.y, phi, st.a);

            float gLaunch, alphaEmitter;
            diskRedshift(st, r, y.y, E, L, gLaunch, alphaEmitter);
            float gTotal = gLaunch * cameraEnergy;
            float gGravity = alphaEmitter / max(float(cameraAlpha), 1e-6f);
            float g = (u_gravitationalRedshiftEnabled == 1 ? gGravity : 1.0f)
                    * (u_dopplerBeamingEnabled > 0.5f ? gTotal / max(gGravity, 1e-6f) : 1.0f);
            // The LUT divides the temperature by its redshift coordinate; 0.5 is the unshifted baseline.
            float lutRedshift = 0.5f / max(g, 1e-3f);
            // I_nu / nu^3 is invariant, so bolometric intensity scales with g^4.
            float intensity = pow(g, 4.0f);

            float opticalDepth = adiskColor(localPos, float(r), float(diskInner), float(diskOuter), mass,
                                            lutRedshift, intensity, colorValue, alpha);
            if (opticalDepth > 0.0f) {
                // Proper length in the gas rest frame: ds = (-p . u_em) dlambda.
                float ds = float(h) / max(gLaunch, 1e-6f);
                alpha *= beerLambert(opticalDepth, ds);
                if (alpha < 0.01f) {
                    g_termination = TERM_DISK;
                    break;
                }
            }
        }
    }

    if (u_debugMode == 4) {
        colorValue = mix(colorValue, ERGOSPHERE_TINT, 1.0f - exp(-ergoPath / mass));
    }

    if (g_termination == TERM_HORIZON || g_termination == TERM_DISK) return colorValue;

    // Direction the light came from: the photon moves along +velocity, so the source lies along -velocity.
    rvec4 dy;
    real dphi, K;
    geodesicRhs(st, E, L, y, dy, dphi, K);
    vec3 skyDirection = normalize(frame * -blVelocityToCartesian(y.x, y.y, phi, st.a, dy.x, dy.y, dphi));

    if (objects && g_termination == TERM_SKY
        && segmentObjectHit(bh.position + frame * blToCartesian(y.x, y.y, phi, st.a), skyDirection, 1e10f, objectColor, hitPoint, hitSolid)) {
        return colorValue + alpha * objectColor;
    }

    return colorValue + alpha * texture(u_skyboxTexture, directionToSpherical(skyDirection)).rgb;
}
