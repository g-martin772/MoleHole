// Diagnostics for the debug views (u_debugMode): steps taken and why the ray ended
const int TERM_SKY = 0;
const int TERM_HORIZON = 1;
const int TERM_OBJECT = 2;
const int TERM_DISK = 3;
const int TERM_STEPS = 4;
const int TERM_CROSSHAIR = 5;
int g_stepCount = 0;
int g_maxStepCount = 1; // budget the step count is shown against
int g_termination = TERM_SKY;

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
            float opticalDepth = adiskColor(vec4(0.0, toSpherical(relativePos)), color, alpha, r_s, newOrigin, u_blackHoles[closestBH].mass, u_blackHoles[closestBH].spinAxis);

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
            if (hit.type == 1) {
                hitSolid = true;
                hitPoint = newOrigin + newDirection * hit.t;
            }
            g_termination = hit.type == 3 ? TERM_CROSSHAIR : TERM_OBJECT;
            return color + hit.color;
        }

        // Check event horizon
        if (distToBH < r_s) {
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
                    if (hit.type == 1) {
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

// ?
rvec4 normalize4Velocity(rvec4 pos, rvec4 vel, real M) {
    real r = pos.y;
    real theta = pos.z;
    real rs = 2.0 * G * M / (c * c);

    real r_s_factor = 1.0 - rs / r;
    if (r_s_factor < 0.001) r_s_factor = 0.001;  // Avoid singularity

    real sinTheta = rsin(theta);
    real v_spatial_sq = (vel.y * vel.y) / r_s_factor +
    vel.z * vel.z * r * r +
    vel.w * vel.w * r * r * sinTheta * sinTheta;

    vel.x = sqrt(max(v_spatial_sq / r_s_factor, 0.0));

    return vel;
}

rvec3 toSphericalReal(rvec3 pos) {
    real r = max(length(pos), real(EPSILON));
    real theta = clamp(racos(pos.y / r), real(EPSILON), real(R_PI) - real(EPSILON));
    real phi = ratan2(pos.z, pos.x);
    return rvec3(r, theta, phi);
}

rvec3 velCartesianToSphericalReal(rvec3 p, rvec3 v) {
    real r = max(length(p), real(EPSILON));
    real rho = max(sqrt(p.x * p.x + p.z * p.z), real(EPSILON));
    real vr = dot(p, v) / r;
    real vtheta = (p.y * (p.x * v.x + p.z * v.z) - rho * rho * v.y) / max(r * r * rho, real(EPSILON));
    real vphi = (p.x * v.z - p.z * v.x) / max(rho * rho, real(EPSILON * EPSILON));
    return clamp(rvec3(vr, vtheta, vphi), rvec3(-1e6), rvec3(1e6));
}

rvec3 velSphericalToCartesianReal(rvec3 p, rvec3 v) {
    real st, ct, sp, cp;
    rsincos_pair(p.y, st, ct);
    rsincos_pair(p.z, sp, cp);
    real r = p.x;
    real vx = v.x * st * cp + r * v.y * ct * cp - r * st * v.z * sp;
    real vy = v.x * ct - r * v.y * st;
    real vz = v.x * st * sp + r * v.y * ct * sp + r * st * v.z * cp;
    return clamp(rvec3(vx, vy, vz), rvec3(-1e6), rvec3(1e6));
}

vec3 rk4RayMarching(vec3 rayOrigin, vec3 rayDirection) {

    vec3 colorValue = vec3(0.0f);
    float alpha = 1.0f;

    if (u_blackHoles[0].mass == 0.0f)
    return texture(u_skyboxTexture, directionToSpherical(rayDirection)).rgb;

    vec3 dir = normalize(rayDirection);

    float stepSize = u_rayStepSize;
    float maxSteps = u_maxRaySteps;
    float adaptiveStepRate = u_adaptiveStepRate;

    real M = real(u_blackHoles[0].mass);
    real a = real(u_blackHoles[0].spin) * M;
    real Q = real(u_blackHoles[0].charge);

    vec3 marchOrigin = rayOrigin;
    {
        float influenceRadius = calculateEventHorizonRadius(u_blackHoles[0].mass) * 25.0f;
        vec3 rel = rayOrigin - u_blackHoles[0].position;
        if (dot(rel, rel) > influenceRadius * influenceRadius) {
            float enterRadius = influenceRadius * 0.999f;
            float b = dot(rel, dir);
            float disc = b * b - (dot(rel, rel) - enterRadius * enterRadius);
            if (b >= 0.0f || disc < 0.0f) {
                return texture(u_skyboxTexture, directionToSpherical(dir)).rgb;
            }
            marchOrigin = rayOrigin + dir * (-b - sqrt(disc));
        }
    }

    rvec3 relativePosCart = rvec3(marchOrigin - u_blackHoles[0].position);

    rvec4 relativePosSph = rvec4(0.0, toSphericalReal(relativePosCart));
    rvec4 relativeDirSph = rvec4(1.0, velCartesianToSphericalReal(relativePosCart, rvec3(dir)));

    float r_s = calculateEventHorizonRadius(u_blackHoles[0].mass);

    // main loop
    g_maxStepCount = int(maxSteps);
    g_termination = TERM_STEPS;
    for (int i = 0; i < maxSteps; i++) {
        g_stepCount++;

        float dist = float(relativePosSph.y);

        if (u_accretionDiskEnabled == 1) {
            float opticalDepth = adiskColor(vec4(relativePosSph), colorValue, alpha, r_s, rayOrigin, u_blackHoles[0].mass, u_blackHoles[0].spinAxis);

            // Apply volumetric absorption using Beer-Lambert law
            if (opticalDepth > 0.0) {
                float transmittance = beerLambert(opticalDepth, stepSize);
                alpha *= transmittance;
                if (alpha < 0.01) {
                    g_termination = TERM_DISK;
                    return colorValue;
                }
            }
        }

        // check if we've hit the event horizon
        if (dist < r_s + 100.0f * EPSILON) {
            g_termination = TERM_HORIZON;
            return vec3(0.0f);
        }

        // check if we've escaped the influence zone
        if (dist > r_s * 25.0f) {
            g_termination = TERM_SKY;
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

        relativeDirSph = normalize4Velocity(relativePosSph, relativeDirSph, M);
        rk4_step(relativePosSph, relativeDirSph, real(stepSize), M, a, Q);
        relativeDirSph = normalize4Velocity(relativePosSph, relativeDirSph, M);
    }

    dir = vec3(velSphericalToCartesianReal(relativePosSph.yzw, relativeDirSph.yzw));

    vec3 color = texture(u_skyboxTexture, directionToSpherical(dir)).rgb;
    return color;
}

// Debug views: colours that do not depend on the scene's own lighting
vec3 debugHeat(float t) {
    t = clamp(t, 0.0, 1.0);
    return clamp(vec3(1.5 * t - 0.25, 1.5 - abs(3.0 * t - 1.5), 1.25 - 1.5 * t), 0.0, 1.0);
}

vec3 debugViewColor(vec3 shaded) {
    if (u_debugMode == 1) {
        // square root keeps both cheap and expensive pixels readable
        return debugHeat(sqrt(float(g_stepCount) / float(max(g_maxStepCount, 1))));
    }
    if (u_debugMode == 2) {
        if (g_termination == TERM_SKY) return vec3(0.04, 0.08, 0.22);
        if (g_termination == TERM_HORIZON) return vec3(0.9, 0.05, 0.05);
        if (g_termination == TERM_OBJECT) return vec3(0.1, 0.8, 0.2);
        if (g_termination == TERM_DISK) return vec3(1.0, 0.6, 0.0);
        if (g_termination == TERM_STEPS) return vec3(1.0, 1.0, 1.0);
        return vec3(0.0, 0.9, 0.9);
    }
    return shaded;
}
