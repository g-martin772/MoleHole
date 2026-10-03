// ------------------------------------------------------------------------------------------------------------
// Section Crosshair
// ------------------------------------------------------------------------------------------------------------
int intersectCrosshair(vec3 rayOrigin, vec3 rayDir, vec3 crossCenter, float crossSize, out float t) {
    t = 1e10;
    int hitAxis = 0;
    float thickness = 0.01f;
    vec3 localOrigin = rayOrigin - crossCenter;

    vec3 oc = localOrigin;
    float a = rayDir.y * rayDir.y + rayDir.z * rayDir.z;
    float b = 2.0 * (oc.y * rayDir.y + oc.z * rayDir.z);
    float c = oc.y * oc.y + oc.z * oc.z - thickness * thickness;
    float discriminant = b * b - 4.0 * a * c;

    if (discriminant >= 0.0 && a > EPSILON) {
        float t0 = (-b - sqrt(discriminant)) / (2.0 * a);
        float t1 = (-b + sqrt(discriminant)) / (2.0 * a);
        float tHit = (t0 > EPSILON) ? t0 : t1;

        if (tHit > EPSILON) {
            vec3 hitPos = localOrigin + rayDir * tHit;
            if (hitPos.x >= 0.0 && hitPos.x <= crossSize) {
                t = min(t, tHit);
                hitAxis = 1;
            }
        }
    }

    oc = localOrigin;
    a = rayDir.x * rayDir.x + rayDir.z * rayDir.z;
    b = 2.0 * (oc.x * rayDir.x + oc.z * rayDir.z);
    c = oc.x * oc.x + oc.z * oc.z - thickness * thickness;
    discriminant = b * b - 4.0 * a * c;

    if (discriminant >= 0.0 && a > EPSILON) {
        float t0 = (-b - sqrt(discriminant)) / (2.0 * a);
        float t1 = (-b + sqrt(discriminant)) / (2.0 * a);
        float tHit = (t0 > EPSILON) ? t0 : t1;

        if (tHit > EPSILON) {
            vec3 hitPos = localOrigin + rayDir * tHit;
            if (hitPos.y >= 0.0 && hitPos.y <= crossSize) {
                if (tHit < t) {
                    t = tHit;
                    hitAxis = 2;
                }
            }
        }
    }

    oc = localOrigin;
    a = rayDir.x * rayDir.x + rayDir.y * rayDir.y;
    b = 2.0 * (oc.x * rayDir.x + oc.y * rayDir.y);
    c = oc.x * oc.x + oc.y * oc.y - thickness * thickness;
    discriminant = b * b - 4.0 * a * c;

    if (discriminant >= 0.0 && a > EPSILON) {
        float t0 = (-b - sqrt(discriminant)) / (2.0 * a);
        float t1 = (-b + sqrt(discriminant)) / (2.0 * a);
        float tHit = (t0 > EPSILON) ? t0 : t1;

        if (tHit > EPSILON) {
            vec3 hitPos = localOrigin + rayDir * tHit;
            if (hitPos.z >= 0.0 && hitPos.z <= crossSize) {
                if (tHit < t) {
                    t = tHit;
                    hitAxis = 3;
                }
            }
        }
    }

    return hitAxis;
}

vec3 getCrosshairNormal(vec3 hitPoint, vec3 crossCenter, int axis) {
    vec3 localPos = hitPoint - crossCenter;
    if (axis == 1) return normalize(vec3(0.0, localPos.y, localPos.z));
    if (axis == 2) return normalize(vec3(localPos.x, 0.0, localPos.z));
    return normalize(vec3(localPos.x, localPos.y, 0.0));
}

vec3 getCrosshairColor(int axis) {
    if (axis == 1) return vec3(1.0, 0.0, 0.0);
    if (axis == 2) return vec3(0.0, 1.0, 0.0);
    if (axis == 3) return vec3(0.0, 0.0, 1.0);
    return vec3(1.0, 1.0, 1.0);
}
