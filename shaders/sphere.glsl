const int MAX_SPHERES = 16;
// uniforms replaced by UBO

// ------------------------------------------------------------------------------------------------------------
// Section Sphere
// ------------------------------------------------------------------------------------------------------------
bool intersectSphere(vec3 rayOrigin, vec3 rayDir, vec3 sphereCenter, float radius, out float t) {
    vec3 oc = rayOrigin - sphereCenter;
    float b = dot(oc, rayDir);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - c;

    if (discriminant < 0.0) return false;

    float sqrtD = sqrt(discriminant);
    float t0 = -b - sqrtD;
    float t1 = -b + sqrtD;

    if (t0 > EPSILON) {
        t = t0;
        return true;
    }
    if (t1 > EPSILON) {
        t = t1;
        return true;
    }
    return false;
}
vec3 renderSphere(vec3 hitPoint, vec3 rayDir, int sphereIndex, vec3 lightDir) {
    vec3 sphereCenter = u_spherePositions[sphereIndex].xyz;
    vec3 normal = normalize(hitPoint - sphereCenter);

    vec3 baseColor = u_sphereColors[sphereIndex].rgb;
    float mass = u_sphereColors[sphereIndex].w;

    if (mass > 0.0) {
        float temp = getTemperatureFromMass(mass);
        if (temp > 0.0) {
            float tempRange = u_lutTempMax - u_lutTempMin;
            float tempNorm = (temp - u_lutTempMin) / tempRange;
            tempNorm = clamp(tempNorm, 0.0, 1.0);

            float redshiftNorm = (1.0 - u_lutRedshiftMin) / (u_lutRedshiftMax - u_lutRedshiftMin);
            redshiftNorm = clamp(redshiftNorm, 0.0, 1.0);

            baseColor = texture(u_blackbodyLUT, vec2(tempNorm, redshiftNorm)).rgb;
        }
    }
    return baseColor;
}