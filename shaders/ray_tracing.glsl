uniform sampler2D u_skyboxTexture;

const int MAX_BLACK_HOLES = 8;
uniform int u_numBlackHoles;
uniform vec3 u_blackHolePositions[MAX_BLACK_HOLES];
uniform float u_blackHoleMasses[MAX_BLACK_HOLES];
uniform float u_blackHoleSpins[MAX_BLACK_HOLES];
uniform vec3 u_blackHoleSpinAxes[MAX_BLACK_HOLES];

uniform int u_renderBlackHoles = 1;
uniform int u_renderSpheres = 1;
uniform int u_accretionDiskEnabled = 1;
uniform int u_gravitationalLensingEnabled = 1;

uniform float u_cubeSize = 1.0;

struct HitRecord {
    bool hit;
    float t;
    vec3 color;
    int type; // 0 = none, 1 = sphere, 2 = mesh, 3 = influence boundary
    int objectIndex;
};

// ------------------------------------------------------------------------------------------------------------
// Section Influence Zone
// ------------------------------------------------------------------------------------------------------------
bool isInInfluenceZone(vec3 pos, out int closestBHIndex, out float distanceToBH, out float influenceRadius) {
    closestBHIndex = -1;
    distanceToBH = 1e10;
    influenceRadius = 0.0;

    if (u_renderBlackHoles == 0) return false;

    for (int j = 0; j < u_numBlackHoles; j++) {
        vec3 relativePos = pos - u_blackHolePositions[j];
        float dist = length(relativePos);
        float r_s = calculateEventHorizonRadius(u_blackHoleMasses[j]);
        float r_i = calculateInfluenceRadius(r_s);

        if (dist < r_i && dist < distanceToBH) {
            closestBHIndex = j;
            distanceToBH = dist;
            influenceRadius = r_i;
        }
    }
    return closestBHIndex >= 0;
}

// ------------------------------------------------------------------------------------------------------------
// Section Ray Tracing
// ------------------------------------------------------------------------------------------------------------
HitRecord rayTraceNormalSpace(vec3 rayOrigin, vec3 rayDir, float maxDistance) {
    HitRecord record;
    record.hit = false;
    record.t = maxDistance;
    record.type = 0;

    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));

    if (u_renderSpheres == 1) {
        for (int i = 0; i < u_numSpheres; i++) {
            float t;
            if (intersectSphere(rayOrigin, rayDir, u_spherePositions[i], u_sphereRadii[i], t)) {
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

// ------------------------------------------------------------------------------------------------------------
// Section Ray Marching
// ------------------------------------------------------------------------------------------------------------
uniform float u_rayStepSize = 0.3f;
uniform int u_maxRaySteps = 1000;
uniform float u_adaptiveStepRate = 0.1f;

vec3 rayMarchInfluenceZone(int closestHole, vec3 rayOrigin, vec3 rayDirection, out bool hitEventHorizon, out bool exitedZone, out vec3 newOrigin, out vec3 newDirection) {
    hitEventHorizon = false;
    exitedZone = false;
    vec3 color = vec3(0.0);
    float alpha = 1.0;

    newOrigin = rayOrigin;
    newDirection = rayDirection;

    float stepSize = u_rayStepSize;
    float maxSteps = u_maxRaySteps;
    float adaptiveStepRate = u_adaptiveStepRate;
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));

    float mass = u_blackHoleMasses[closestHole];
    stepSize *= 1.0f * mass;
    maxSteps *= 1.0f * mass;
    adaptiveStepRate *= 1.0f * mass;

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

        vec3 relativePos = newOrigin - u_blackHolePositions[closestBH];

        // Calculate orbital angular momentum
        vec3 orbitalAngMomentum = cross(relativePos, newDirection);
        vec3 bhAngMomentum = calculateAngularMomentumFromSpin(
            u_blackHoleSpins[closestBH],
            u_blackHoleSpinAxes[closestBH],
            u_blackHoleMasses[closestBH]
        );
        vec3 totalAngMomentum = orbitalAngMomentum + bhAngMomentum * 0.1;
        float angMomSqrd = dot(totalAngMomentum, totalAngMomentum);

        float r_s = calculateEventHorizonRadius(u_blackHoleMasses[closestBH]);

        // Adaptive step size
        float currentStepSize = stepSize * min(adaptiveStepRate, distToBH / r_s);

        if (u_gravitationalLensingEnabled == 1) {
            vec3 acceleration = calculateAccelerationLUT(angMomSqrd, relativePos);
            newDirection += acceleration * currentStepSize;
            newDirection = normalize(newDirection);
        }

        if (u_accretionDiskEnabled == 1) {
            // Get optical depth from the accretion disk at this position
            float opticalDepth = adiskColor(relativePos, color, alpha, r_s, newOrigin, u_blackHoleMasses[closestBH]);

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

// ------------------------------------------------------------------------------------------------------------
// Section Hybrid Ray Marching + Tracing
// ------------------------------------------------------------------------------------------------------------
vec3 hybridRayTrace(vec3 rayOrigin, vec3 rayDirection) {
    vec3 color = vec3(0.0);
    vec3 currentOrigin = rayOrigin;
    vec3 currentDir = rayDirection;

    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));

    // Use a fraction of max steps for outer loop to allow for multiple influence zone passes
    int maxOuterIterations = max(1000, u_maxRaySteps / 2);

    for (int iter = 0; iter < maxOuterIterations; iter++) {
        int closestBH;
        float distToBH;
        float influenceR;
        bool inZone = isInInfluenceZone(currentOrigin, closestBH, distToBH, influenceR);

        if (inZone) {
            bool hitHorizon, exited;
            vec3 newOrigin, newDir;
            vec3 marchColor = rayMarchInfluenceZone(closestBH, currentOrigin, currentDir, hitHorizon, exited, newOrigin, newDir);
            color += marchColor;

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
                    vec3 toCenter = u_blackHolePositions[j] - currentOrigin;
                    float distToCenter = length(toCenter);
                    float r_s = calculateEventHorizonRadius(u_blackHoleMasses[j]);
                    float r_i = calculateInfluenceRadius(r_s);

                    float distToInfluence = abs(distToCenter - r_i);
                    minDistToInfluence = min(minDistToInfluence, distToInfluence);
                }
            }

            HitRecord hit = rayTraceNormalSpace(currentOrigin, currentDir, 1e10);

            if (hit.hit) {
                if (hit.t < minDistToInfluence) {
                    color += hit.color;
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