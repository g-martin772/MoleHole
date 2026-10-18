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

// alpha is the transmittance of everything in front (disk gas); it carries over between zones.
vec3 rayMarchInfluenceZone(int closestHole, vec3 rayOrigin, vec3 rayDirection, out bool hitEventHorizon, out bool exitedZone, out vec3 newOrigin, out vec3 newDirection, out vec3 hitPoint, out bool hitSolid, inout float alpha) {
    hitEventHorizon = false;
    exitedZone = false;
    hitSolid = false;
    hitPoint = vec3(0.0);
    vec3 color = vec3(0.0);

    newOrigin = rayOrigin;
    newDirection = rayDirection;

    float stepSize = u_rayStepSize * 10.0f;
    float maxSteps = u_maxRaySteps / 50;
    float adaptiveStepRate = u_adaptiveStepRate;

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
        float currentStepSize = stepSize * u_blackHoles[closestBH].mass * min(adaptiveStepRate, distToBH / r_s);

        if (u_gravitationalLensingEnabled == 1) {
            vec3 acceleration = calculateAcceleration(angMomSqrd, relativePos, u_blackHoles[closestBH].mass);
            newDirection += acceleration * currentStepSize;
            newDirection = normalize(newDirection);
        }

        if (u_accretionDiskEnabled == 1) {
            BlackHoleData bh = u_blackHoles[closestBH];
            Spacetime st = spacetimeOf(bh);
            mat3 toLocal = transpose(spinFrame(bh.spinAxis));
            vec3 axis = normalize(bh.spinAxis);
            vec4 diskInfo = u_diskInfo[closestBH];
            // The analytic lens is a Schwarzschild-like approximation that breaks down inside the Schwarzschild
            // photon sphere (3 M), so its disk starts no closer than that even when the ISCO of a fast spin does.
            diskInfo.y = max(diskInfo.y, 1.5 * r_s);
            float temperature, column;

            if (u_accretionDiskVolumetric == 1) {
                float r = length(relativePos);
                vec3 localPos = toLocal * relativePos;
                float H = diskScaleHeight(st, r);
                float zOverH = localPos.z / H;
                if (r >= diskInfo.y && r <= diskInfo.z && abs(zOverH) < 4.0) {
                    diskGas(st, closestBH, r, atan(localPos.y, localPos.x), zOverH, temperature, column);
                    float g = analyticLensDiskG(st, relativePos, newDirection, bh.spinAxis);
                    diskAccumulate(diskVolumeAbsorption(column, zOverH, H) * currentStepSize,
                                   diskSourceRadiance(closestBH, temperature, g), color, alpha);
                }
            } else {
                // Thin disk: shade exactly where this step crosses the equatorial plane.
                vec3 nextPos = relativePos + newDirection * currentStepSize;
                float z0 = dot(relativePos, axis);
                float z1 = dot(nextPos, axis);
                if (z0 * z1 < 0.0) {
                    vec3 crossing = mix(relativePos, nextPos, z0 / (z0 - z1));
                    float r = length(crossing);
                    if (r >= diskInfo.y && r <= diskInfo.z) {
                        vec3 localPos = toLocal * crossing;
                        diskGas(st, closestBH, r, atan(localPos.y, localPos.x), 0.0, temperature, column);
                        float g = analyticLensDiskG(st, crossing, newDirection, bh.spinAxis);
                        diskAccumulate(diskSheetDepth(column, abs(dot(newDirection, axis))),
                                       diskSourceRadiance(closestBH, temperature, g), color, alpha);
                    }
                }
            }
            if (alpha < 0.01) {
                g_termination = TERM_DISK;
                return color;
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
            return color + alpha * hit.color;
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

// A static observer at distance D from a mass M sees a photon of impact parameter b under
// sin(psi) = b sqrt(1 - 2M/D) / D, while the Binet integrator below works with sin(psi') = b / D.
// Converts the pixel direction (psi) into the integrator's direction (psi'), keeping its sense along the radius.
vec3 staticObserverDirection(vec3 origin, vec3 dir) {
    if (u_renderBlackHoles == 0 || u_numBlackHoles == 0) return dir;
    vec3 rel = origin - u_blackHoles[0].position;
    float distance = length(rel);
    float k = 1.0 - 2.0 * u_blackHoles[0].mass / distance;
    if (k <= 0.0 || distance <= 0.0) return dir;
    vec3 inward = -rel / distance;
    float cosPsi = dot(dir, inward);
    vec3 perpendicular = dir - cosPsi * inward;
    float sinPsi = length(perpendicular);
    if (sinPsi < 1e-6) return dir;
    float sinNew = min(sinPsi / sqrt(k), 1.0);
    float cosNew = (cosPsi < 0.0 ? -1.0 : 1.0) * sqrt(1.0 - sinNew * sinNew);
    return normalize(cosNew * inward + sinNew * perpendicular / sinPsi);
}

vec3 hybridRayTrace(vec3 rayOrigin, vec3 rayDirection, out vec3 hitPoint, out bool hitSolid) {
    vec3 color = vec3(0.0);
    float alpha = 1.0;
    vec3 currentOrigin = rayOrigin;
    vec3 currentDir = staticObserverDirection(rayOrigin, rayDirection);
    hitSolid = false;
    hitPoint = vec3(0.0);

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
            vec3 marchColor = rayMarchInfluenceZone(closestBH, currentOrigin, currentDir, hitHorizon, exited, newOrigin, newDir, zoneHitPoint, zoneHitSolid, alpha);
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
                    color += alpha * hit.color;
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

            color += alpha * texture(u_skyboxTexture, directionToSpherical(currentDir)).rgb;
            return color;
        }
    }

    g_termination = TERM_STEPS;
    color += alpha * texture(u_skyboxTexture, directionToSpherical(currentDir)).rgb;
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


// Frequency ratio g = nu_camera / nu_emitted for disk gas at (r, theta) on the integrated path, honouring the
// redshift / Doppler toggles: the gravitational part is the lapse ratio, the rest is Doppler and frame dragging.
// gLaunch is the ratio relative to the launch ZAMO, which converts affine steps into proper lengths.
float integratedDiskG(Spacetime st, real r, real theta, real E, real L, float cameraEnergy, float cameraAlpha,
                      out float gLaunch) {
    float alphaEmitter;
    diskRedshift(st, r, theta, E, L, gLaunch, alphaEmitter);
    float gTotal = gLaunch * cameraEnergy;
    float gGravity = alphaEmitter / max(cameraAlpha, 1e-6f);
    return (u_gravitationalRedshiftEnabled == 1 ? gGravity : 1.0f)
         * (u_dopplerBeamingEnabled > 0.5f ? gTotal / max(gGravity, 1e-6f) : 1.0f);
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
    vec4 diskInfo = u_diskInfo[0];
    real diskOuter = real(diskInfo.z);
    real maxStep = real(10.0f * u_rayStepSize * mass);
    bool volumetricDisk = u_accretionDiskEnabled == 1 && u_accretionDiskVolumetric == 1;

    float ergoPath = 0.0f;
    float temperature, column;

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
        // Resolve the vertical profile of the volumetric disk: at most a quarter scale height per step inside it.
        if (volumetricDisk && r < real(1.1) * diskOuter) {
            float H = diskScaleHeight(st, float(r));
            if (abs(float(r * rcos(y.y))) < 5.0f * H) h = min(h, real(0.25f * H));
        }

        vec3 chordStart = objects ? blToCartesian(y.x, y.y, phi, st.a) : vec3(0.0f);
        real r0 = y.x;
        real phi0 = phi;
        real pth0 = y.w;
        real ct0 = rcos(y.y);
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

        // Equatorial-plane crossing of this step, linearly interpolated in cos(theta).
        real ct = rcos(y.y);
        bool crossed = sign(ct) != sign(ct0);
        real f = crossed ? ct0 / (ct0 - ct) : real(0.0);
        real rCross = mix(r0, y.x, f);

        if (u_debugMode == 4) {
            real rErgo = st.M + sqrt(max(st.M * st.M - st.a * st.a * ct * ct - st.Q * st.Q, real(0.0)));
            if (r < rErgo) ergoPath += float(h);
            if (crossed && abs(rCross - st.rIsco) < real(0.1) * st.M) {
                colorValue += ISCO_RING_COLOR * alpha;
            }
        }

        if (u_accretionDiskEnabled == 1) {
            float gLaunch;
            if (volumetricDisk) {
                if (r >= real(diskInfo.y) && r <= diskOuter) {
                    vec3 localPos = blToCartesian(r, y.y, phi, st.a);
                    float H = diskScaleHeight(st, float(r));
                    float zOverH = localPos.z / H;
                    if (abs(zOverH) < 4.0f) {
                        float g = integratedDiskG(st, r, y.y, E, L, cameraEnergy, float(cameraAlpha), gLaunch);
                        diskGas(st, 0, float(r), float(phi), zOverH, temperature, column);
                        // Proper length through the gas: ds = (-p . u_em) dlambda.
                        float ds = float(h) / max(gLaunch, 1e-6f);
                        diskAccumulate(diskVolumeAbsorption(column, zOverH, H) * ds,
                                       diskSourceRadiance(0, temperature, g), colorValue, alpha);
                    }
                }
            } else if (crossed && rCross >= real(diskInfo.y) && rCross <= diskOuter) {
                real halfPi = real(R_PI) * real(0.5);
                float g = integratedDiskG(st, rCross, halfPi, E, L, cameraEnergy, float(cameraAlpha), gLaunch);
                // |cos| of the ray to the disk normal in the local ZAMO frame: p^(theta) / E_local.
                real zamoAlpha, zamoOmega;
                zamo(st, rCross, halfPi, zamoAlpha, zamoOmega);
                real localEnergy = (E - zamoOmega * L) / max(zamoAlpha, real(1e-6));
                float mu = float(abs(mix(pth0, y.w, f)) / (rCross * max(localEnergy, real(1e-6))));
                diskGas(st, 0, float(rCross), float(mix(phi0, phi, f)), 0.0f, temperature, column);
                diskAccumulate(diskSheetDepth(column, mu), diskSourceRadiance(0, temperature, g), colorValue, alpha);
            }
            if (alpha < 0.01f) {
                g_termination = TERM_DISK;
                break;
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
