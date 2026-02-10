// ------------------------------------------------------------------------------------------------------------
// Section SDF and FBM
// ------------------------------------------------------------------------------------------------------------
float smin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}
float smax(float a, float b, float k) {
    return -smin(-a, -b, k);
}
float sdfSphere(ivec3 i, vec3 f, ivec3 c) {
    // Random radius at grid vertex i+c
    float rad = 0.5 * hash(i + c);
    // Distance to sphere at grid vertex i+c
    return length(f - vec3(c)) - rad;
}
float sdBase(vec3 p) {
    ivec3 i = ivec3(floor(p));
    vec3 f = fract(p);

    // Distance to the 8 corner spheres
    return min(min(min(sdfSphere(i, f, ivec3(0,0,0)),
                       sdfSphere(i, f, ivec3(0,0,1))),
                   min(sdfSphere(i, f, ivec3(0,1,0)),
                       sdfSphere(i, f, ivec3(0,1,1)))),
               min(min(sdfSphere(i, f, ivec3(1,0,0)),
                       sdfSphere(i, f, ivec3(1,0,1))),
                   min(sdfSphere(i, f, ivec3(1,1,0)),
                       sdfSphere(i, f, ivec3(1,1,1)))));
}
float sdFbm(vec3 p, float d) {
    float s = 1.0;
    for (int i = 0; i < 7; i++) {
        // Evaluate new octave
        float n = s * sdBase(p);

        // Add using smooth min/max
        n = smax(n, d - 0.1 * s, 0.3 * s);
        d = smin(n, d, 0.3 * s);

        // Prepare next octave - rotation matrix for varied detail
        p = mat3(0.00, 1.60, 1.20,
        -1.60, 0.72, -0.96,
        -1.20, -0.96, 1.28) * p;
        s = 0.5 * s;
    }
    return d;
}