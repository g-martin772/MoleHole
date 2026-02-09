// ------------------------------------------------------------------------------------------------------------
// Section Crosshair
// ------------------------------------------------------------------------------------------------------------
bool intersectCube(vec3 rayOrigin, vec3 rayDir, vec3 cubeCenter, float cubeSize, out float t) {
    vec3 boxMin = cubeCenter - vec3(cubeSize * 0.5);
    vec3 boxMax = cubeCenter + vec3(cubeSize * 0.5);
    vec3 tMin = (boxMin - rayOrigin) / rayDir;
    vec3 tMax = (boxMax - rayOrigin) / rayDir;
    vec3 t1 = min(tMin, tMax);
    vec3 t2 = max(tMin, tMax);
    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);

    if (tNear > tFar || tFar < EPSILON) return false;
    t = (tNear > EPSILON) ? tNear : tFar;
    return t > EPSILON;
}
vec3 getCubeNormal(vec3 hitPoint, vec3 cubeCenter, float cubeSize) {
    vec3 localPos = hitPoint - cubeCenter;
    vec3 absPos = abs(localPos);
    float halfSize = cubeSize * 0.5;

    if (absPos.x > absPos.y && absPos.x > absPos.z) {
        return vec3(sign(localPos.x), 0.0, 0.0);
    } else if (absPos.y > absPos.z) {
        return vec3(0.0, sign(localPos.y), 0.0);
    } else {
        return vec3(0.0, 0.0, sign(localPos.z));
    }
}