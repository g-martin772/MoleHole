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

#include "body.glsl"

// Stars (a rigid body mass with a main-sequence temperature) glow in their blackbody colour; every other sphere is a
// lit surface (shaders/body.glsl). N is the outward surface normal, hitDistance the eye's distance to it.
vec3 renderSphereSurface(int sphereIndex, vec3 N, vec3 rayDir, float hitDistance, vec3 lightDir) {
    vec3 baseColor = u_spheres[sphereIndex].color.rgb;
    float mass = u_spheres[sphereIndex].mass;

    if (u_spheres[sphereIndex].emissive > 0.5) {
        float mu = max(dot(normal, -rayDir), 0.0);
        return baseColor * (1.0 - u_spheres[sphereIndex].limbDarkening * (1.0 - mu));
    }

    if (mass > 0.0) {
        float temp = getTemperatureFromMass(mass);
        if (temp > 0.0) {
            float tempRange = u_lutTempMax - u_lutTempMin;
            float tempNorm = (temp - u_lutTempMin) / tempRange;
            tempNorm = clamp(tempNorm, 0.0, 1.0);

            float redshiftNorm = (1.0 - u_lutRedshiftMin) / (u_lutRedshiftMax - u_lutRedshiftMin);
            redshiftNorm = clamp(redshiftNorm, 0.0, 1.0);

            return texture(u_blackbodyLUT, vec2(tempNorm, redshiftNorm)).rgb;
        }
    }
    // Spheres without a Surface component are markers (orbits, demos): flat colour, optionally textured, unlit
    if (u_bodies[sphereIndex].flags.x == 0) {
        BodyData body = u_bodies[sphereIndex];
        if (body.maps.x < 0) return baseColor;
        vec2 uv = equirectUv(quatRotate(quatConjugate(body.orientation), N));
        float pixelSpread = 2.0 * tan(radians(u_fov) * 0.5) / float(imageSize(l_outputImage).y);
        return baseColor * sampleBodyMap(body.maps.x, uv, pixelSpread * hitDistance, u_spheres[sphereIndex].radius,
                                         abs(dot(N, rayDir)), vec4(1.0)).rgb;
    }
    // Irradiance pi: a white Lambertian surface facing the light shows 1.0
    return shadeBodySurface(sphereIndex, N, rayDir, hitDistance, baseColor, lightDir, vec3(BODY_PI));
}

vec3 renderSphere(vec3 hitPoint, vec3 rayDir, int sphereIndex, vec3 lightDir) {
    return renderSphereSurface(sphereIndex, normalize(hitPoint - u_spheres[sphereIndex].position), rayDir,
                               distance(u_cameraPos, hitPoint), lightDir);
}
