layout(binding = 2) uniform sampler2D u_skyboxTexture;

struct ShaderTriangle {
    vec4 v0;
    vec4 v1;
    vec4 v2;
    vec4 color;
};

layout(std140, binding = 10) readonly buffer MeshBuffer {
    ShaderTriangle triangles[];
} u_meshBuffer;

const int MAX_BLACK_HOLES = 8;

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
        vec3 relativePos = pos - u_blackHolePositions[j].xyz;
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
            float t = 1e10;
            if (intersectSphere(rayOrigin, rayDir, u_spherePositions[i].xyz, u_spherePositions[i].w, t)) {
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

    // Mesh Intersection
    int numTriangles = u_meshBuffer.triangles.length();
    for (int i = 0; i < numTriangles; i++) {
         vec3 v0 = u_meshBuffer.triangles[i].v0.xyz;
         vec3 v1 = u_meshBuffer.triangles[i].v1.xyz;
         vec3 v2 = u_meshBuffer.triangles[i].v2.xyz;
         vec3 triColor = u_meshBuffer.triangles[i].color.rgb;
         
         // Moller-Trumbore intersection
         vec3 e1 = v1 - v0;
         vec3 e2 = v2 - v0;
         vec3 h = cross(rayDir, e2);
         float a = dot(e1, h);
         
         if (a > -1e-6 && a < 1e-6) continue;
         
         float f = 1.0 / a;
         vec3 s = rayOrigin - v0;
         float u = f * dot(s, h);
         
         if (u < 0.0 || u > 1.0) continue;
         
         vec3 q = cross(s, e1);
         float v = f * dot(rayDir, q);
         
         if (v < 0.0 || u + v > 1.0) continue;
         
         float t = f * dot(e2, q);
         
         if (t > 1e-4 && t < record.t) {
             record.hit = true;
             record.t = t;
             record.type = 2; // Mesh
             record.objectIndex = i;
             
             // Simple lighting
             vec3 normal = normalize(cross(e1, e2));
             float diffuse = max(dot(normal, lightDir), 0.0);
             record.color = triColor * (0.2 + diffuse * 0.8);
         }
    }

    if (u_enableThirdPerson == 1) {
        float t;
        // intersectCube and getCubeNormal are missing
        /*
        if (intersectCube(rayOrigin, rayDir, u_cameraPos.xyz, u_cubeSize, t)) {
            if (t < record.t) {
                record.hit = true;
                record.t = t;
                record.type = 3; // cube type
                record.objectIndex = 0;

                vec3 hitPoint = rayOrigin + rayDir * t;
                vec3 normal = getCubeNormal(hitPoint, u_cameraPos.xyz, u_cubeSize);

                // Simple Lambertian shading for the cube
                vec3 cubeColor = vec3(0.7, 0.3, 0.2); // Orange-ish color
                float diffuse = max(dot(normal, lightDir), 0.0);
                vec3 ambient = vec3(0.1);
                record.color = cubeColor * (ambient + diffuse * 0.9);
            }
        }
        */
    }
    return record;
}

// ------------------------------------------------------------------------------------------------------------
// Section Ray Marching
// ------------------------------------------------------------------------------------------------------------

vec3 getAcceleration(vec3 pos, vec3 dir, int bhIndex) {
    vec3 relativePos = pos - u_blackHolePositions[bhIndex].xyz;
    // Calculate orbital angular momentum L = r x v
    vec3 orbitalAngMomentum = cross(relativePos, dir);
    
    // Approximation: We use the primary BH's spin for the "local" metric influence
    vec3 bhAngMomentum = calculateAngularMomentumFromSpin(
        u_blackHoleSpins[bhIndex].x,
        u_blackHoleSpinAxes[bhIndex].xyz,
        u_blackHoleMasses[bhIndex]
    );
    // 0.1 factor is an empirical scaling for the frame-dragging effect strength in this approximation
    vec3 totalAngMomentum = orbitalAngMomentum + bhAngMomentum * 0.1;
    float angMomSqrd = dot(totalAngMomentum, totalAngMomentum);

    // Note: This uses a precomputed LUT which approximates the Kerr metric acceleration.
    // It is physically based but pre-baked for performance.
    return calculateAccelerationLUT(angMomSqrd, relativePos);
}

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
    stepSize *= 1.0f * mass; // Scale step size by mass
    // maxSteps *= 1.0f * mass; // Don't scale steps, just size
    adaptiveStepRate *= 1.0f * mass;

    // volumetric
    float visibility = 1.0f;
    float volumeDepth = 0.0f;

    for (int i = 0; i < maxSteps; i++) {
        int closestBH;
        float distToBH;
        float influenceR;
        // In influence zone?
        bool inZone = isInInfluenceZone(newOrigin, closestBH, distToBH, influenceR);

        if (!inZone) {
            exitedZone = true;
            return color;
        }

        vec3 relativePos = newOrigin - u_blackHolePositions[closestBH].xyz;
        float r_s = calculateEventHorizonRadius(u_blackHoleMasses[closestBH]);

        // Check event horizon
        if (distToBH < r_s) {
            hitEventHorizon = true;
            return color;
        }

        // Adaptive step size
        float currentStepSize = stepSize * min(adaptiveStepRate, max(0.01, distToBH / r_s));
        
        vec3 startOrigin = newOrigin;
        vec3 startDirection = newDirection;
        vec3 nextOrigin;
        vec3 nextDirection;

        if (u_gravitationalLensingEnabled == 1) {
            // RK4 Integration for physically accurate geodesics
            vec3 k1_v = getAcceleration(newOrigin, newDirection, closestBH);
            vec3 k1_x = newDirection;

            vec3 k2_v = getAcceleration(newOrigin + k1_x * currentStepSize * 0.5, newDirection + k1_v * currentStepSize * 0.5, closestBH);
            vec3 k2_x = newDirection + k1_v * currentStepSize * 0.5;

            vec3 k3_v = getAcceleration(newOrigin + k2_x * currentStepSize * 0.5, newDirection + k2_v * currentStepSize * 0.5, closestBH);
            vec3 k3_x = newDirection + k2_v * currentStepSize * 0.5;

            vec3 k4_v = getAcceleration(newOrigin + k3_x * currentStepSize, newDirection + k3_v * currentStepSize, closestBH);
            vec3 k4_x = newDirection + k3_v * currentStepSize;

            vec3 dirDelta = (currentStepSize / 6.0) * (k1_v + 2.0*k2_v + 2.0*k3_v + k4_v);
            vec3 originDelta = (currentStepSize / 6.0) * (k1_x + 2.0*k2_x + 2.0*k3_x + k4_x);
            
            nextDirection = normalize(newDirection + dirDelta);
            nextOrigin = newOrigin + originDelta;
        } else {
             nextDirection = newDirection;
             nextOrigin = newOrigin + newDirection * currentStepSize;
        }

        if (u_accretionDiskEnabled == 1) {
            // Get optical depth from the accretion disk at this position
            float opticalDepth = adiskColor(relativePos, color, alpha, r_s, startOrigin, u_blackHoleMasses[closestBH]);

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
        HitRecord hit = rayTraceNormalSpace(startOrigin, startDirection, currentStepSize);
        if (hit.hit) {
            return color + hit.color;
        }
        
        // Update state
        newOrigin = nextOrigin;
        newDirection = nextDirection;
    }
    return color;
}

// ------------------------------------------------------------------------------------------------------------
// Section Hybrid Ray Marching + Tracing
// ------------------------------------------------------------------------------------------------------------
vec3 hybridRayTrace(vec3 rayOrigin, vec3 rayDir) {
    // Mode 0: Straight Rays (Euclidean)
    if (u_rayTracingMode == 0) {
        // Trace against all objects in the scene with infinite distance
        HitRecord hit = rayTraceNormalSpace(rayOrigin, rayDir, 1e20);
        if (hit.hit) {
            return hit.color;
        }
        return texture(u_skyboxTexture, directionToSpherical(rayDir)).rgb;
    }

    // Mode 1: Curved Rays (Kerr Geodesic via Ray Marching)
    vec3 color = vec3(0.0);
    vec3 currentOrigin = rayOrigin;
    vec3 currentDir = rayDir;

    // Use a fixed max iteration count to prevent infinite loops in the hybrid logic
    for (int iter = 0; iter < 100; iter++) {
        int closestBH;
        float distToBH;
        float influenceR;
        
        bool inZone = isInInfluenceZone(currentOrigin, closestBH, distToBH, influenceR);

        if (inZone) {
            bool hitHorizon;
            bool exited;
            vec3 newOrigin;
            vec3 newDir;
            vec3 marchColor = rayMarchInfluenceZone(closestBH, currentOrigin, currentDir, hitHorizon, exited, newOrigin, newDir);
            
            // Add any emission from marching (accretion disk)
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
            // Ray Tracing in Normal Space (Euclidean)
            float minDistToInfluence = 1e10;
            if (u_renderBlackHoles == 1) {
                for (int j = 0; j < u_numBlackHoles; j++) {
                    float r_s = calculateEventHorizonRadius(u_blackHoleMasses[j]);
                    float r_i = calculateInfluenceRadius(r_s);
                    float dist = length(currentOrigin - u_blackHolePositions[j].xyz) - r_i;
                    if (dist > 0.0) minDistToInfluence = min(minDistToInfluence, dist);
                }
            }

            // Check intersections with objects in normal space
            HitRecord hit = rayTraceNormalSpace(currentOrigin, currentDir, minDistToInfluence);

            if (hit.hit) {
                return color + hit.color;
            }

            // If we didn't hit anything and we are far from influence zones, check for distant objects one last time, then hit skybox
            if (minDistToInfluence > 1000.0) {
                HitRecord hit = rayTraceNormalSpace(currentOrigin, currentDir, 1e20);
                if (hit.hit) {
                    return color + hit.color;
                }
                return color + texture(u_skyboxTexture, directionToSpherical(currentDir)).rgb;
            }

            // Advance ray to influence zone boundary
            currentOrigin += currentDir * (minDistToInfluence + EPSILON);
        }
    }
    
    return color + texture(u_skyboxTexture, directionToSpherical(currentDir)).rgb;
}