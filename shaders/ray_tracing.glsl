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
uniform float u_rayStepSize = 0.1f;
uniform int u_maxRaySteps = 1000;
uniform float u_adaptiveStepRate = 0.5f;

vec3 rayMarchInfluenceZone(int closestHole, vec3 rayOrigin, vec3 rayDirection, out bool hitEventHorizon, out bool exitedZone, out vec3 newOrigin, out vec3 newDirection) {
    hitEventHorizon = false;
    exitedZone = false;
    float stepSize = u_rayStepSize;
    float maxSteps = u_maxRaySteps;
    float adaptiveStepRate = u_adaptiveStepRate;
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 color = vec3(0.0f);
    float alpha = 1.0f;
    float mass = u_blackHoleMasses[closestHole];

    newOrigin = rayOrigin;
    newDirection = rayDirection;

    for (int i = 0; i < maxSteps; i++) {

        int closestBH;
        float distToBH;
        float influenceR;
        bool inZone = isInInfluenceZone(newOrigin, closestBH, distToBH, influenceR);

        if (!inZone) {
            exitedZone = true;
            return color;
        }
        vec3 bhPos = u_blackHolePositions[closestBH];
        float r_s = calculateEventHorizonRadius(u_blackHoleMasses[closestBH]);

        vec3 relativePosCart = newOrigin - bhPos;
        vec3 relativePosSph = toSpherical(relativePosCart);
        vec3 relativeDirSph = vel_cartesian_to_spherical(relativePosCart, newDirection);
        distToBH = relativePosSph.x - r_s;

        if (distToBH < r_s) {
            hitEventHorizon = true;
            return color;
        }

        vec4 p = vec4(0.0f, relativePosSph);
        vec4 v = vec4(1.0f, relativeDirSph);
        // compute force of current closest bh to correct trajectories
        rk4_step(p, v, stepSize, u_blackHoleMasses[closestBH]);

        relativePosCart = toCartesian(p.yzw);
        newDirection = vel_spherical_to_cartesian(p.yzw, v.yzw);
        newOrigin = relativePosCart + bhPos;
    }
    return color;
}

// ------------------------------------------------------------------------------------------------------------
// Section Hybrid Ray Marching + Tracing
// ------------------------------------------------------------------------------------------------------------
vec3 hybridRayTrace(vec3 rayOrigin, vec3 rayDirection) {

    if (u_blackHoleMasses[0] == 0.0f)
        return texture(u_skyboxTexture, directionToSpherical(rayDirection)).rgb;

    // ray position and direction (Cartesian)
    vec3 pos = rayOrigin;
    vec3 dir = normalize(rayDirection);

    // loop variables
    float stepSize = u_rayStepSize;
    float maxSteps = u_maxRaySteps;
    float adaptiveStepRate = u_adaptiveStepRate;

    // compute event horizon radius
    float r_s = calculateEventHorizonRadius(u_blackHoleMasses[0]);

    // main loop
    for (int i = 0; i < maxSteps; i++) {

        // position relative to black hole
        vec3 relativePosCart = pos - u_blackHolePositions[0];

        // check if we've hit the event horizon
        float dist = length(relativePosCart);
        if (dist < r_s) {
            return vec3(1.0f, 0.0f, 0.0f);  // Red for event horizon
        }

        // convert to spherical coordinates
        vec3 relativePosSph = toSpherical(relativePosCart);
        vec3 relativeDirSph = vel_cartesian_to_spherical(relativePosCart, dir);

        // adaptive step size near horizon
        if (dist < r_s * 2.0f) {
            stepSize = u_rayStepSize * adaptiveStepRate;
        } else {
            stepSize = u_rayStepSize;
        }

        // geodesic integration (RK4)
        vec4 p = vec4(0.0f, relativePosSph);
        vec4 v = vec4(1.0f, relativeDirSph);
        rk4_step(p, v, stepSize, u_blackHoleMasses[0]);

        // convert back to Cartesian coordinates
        relativePosCart = toCartesian(p.yzw);
        dir = vel_spherical_to_cartesian(p.yzw, v.yzw);

        // update global position
        pos = relativePosCart + u_blackHolePositions[0];

        // check if we've escaped the influence zone
        float distFromBH = length(pos - u_blackHolePositions[0]);
        float influenceRadius = calculateInfluenceRadius(r_s);
        if (distFromBH > influenceRadius) {
            break;  // Ray has escaped
        }
    }

    // map skybox color from final ray direction
    vec3 color = texture(u_skyboxTexture, directionToSpherical(dir)).rgb;
    return color;
}