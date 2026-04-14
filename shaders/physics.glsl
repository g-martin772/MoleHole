const float BOLTZMANN = 5.670374419e-8;

// ------------------------------------------------------------------------------------------------------------
// Section Event Horizon
// ------------------------------------------------------------------------------------------------------------
float calculateEventHorizonRadius(float mass) {
    return 2.0f * mass * G / pow(c, 2);
}

// ------------------------------------------------------------------------------------------------------------
// Section Influence Radius
// ------------------------------------------------------------------------------------------------------------
float calculateInfluenceRadius(float r_s) {
    return 50.0f * r_s;
}

// ------------------------------------------------------------------------------------------------------------
// Section Angular Momentum
// ------------------------------------------------------------------------------------------------------------
vec3 calculateAngularMomentumFromSpin(float spin, vec3 spinAxis, float mass) {
    float h = spin * mass;
    return h * normalize(spinAxis);
}

// ------------------------------------------------------------------------------------------------------------
// Section Temperature
// ------------------------------------------------------------------------------------------------------------
float getAccretionDiskTemperature(float mass, float radius, float inner_radius) {
    float constant = (3.0f * mass) / (8 * PI * BOLTZMANN);
    float innerTerm = constant * pow(radius, -3.0f) * (1.0f - sqrt(inner_radius / radius));
    return pow(innerTerm, 0.25f);
}

// ------------------------------------------------------------------------------------------------------------
// Section Density
// ------------------------------------------------------------------------------------------------------------
float beerLambert(float abCoeff, float dist) {
    return exp(-abCoeff * dist);
}

// ------------------------------------------------------------------------------------------------------------
// Section Coordinates
// ------------------------------------------------------------------------------------------------------------
vec3 toSpherical(vec3 pos) {
    float r = length(pos);
    if (r < EPSILON) r = EPSILON;

    float theta = acos(clamp(pos.y / r, -1.0, 1.0));

    if (theta < EPSILON) theta = EPSILON;
    if (theta > (PI - EPSILON)) theta = (PI - EPSILON);

    float phi = atan(pos.z, pos.x);
    return vec3(r, theta, phi);
}
vec3 toCartesian(vec3 spherical) {
    float r = spherical.x;
    float theta = spherical.y;
    float phi = spherical.z;

    float x = r * sin(theta) * cos(phi);
    float y = r * cos(theta);
    float z = r * sin(theta) * sin(phi);

    return vec3(x, y, z);
}
vec3 vel_cartesian_to_spherical(vec3 p_cart, vec3 v_cart) {
    float x = p_cart.x;
    float y = p_cart.y;
    float z = p_cart.z;
    float vx = v_cart.x;
    float vy = v_cart.y;
    float vz = v_cart.z;

    float r = length(p_cart);
    if (r < EPSILON) r = EPSILON;

    float rho = sqrt(x * x + z * z);
    if (rho < EPSILON) rho = EPSILON;

    float vr = (x * vx + y * vy + z * vz) / r;

    // Safety: clamp intermediate calculations to prevent overflow
    float numerator_vtheta = y * (x * vx + z * vz) - rho * rho * vy;
    float denominator_vtheta = r * r * rho;
    float vtheta = numerator_vtheta / max(denominator_vtheta, EPSILON);

    float vphi = (x * vz - z * vx) / max(rho * rho, EPSILON * EPSILON);

    // Safety: clamp extreme values
    vr = clamp(vr, -1e6, 1e6);
    vtheta = clamp(vtheta, -1e6, 1e6);
    vphi = clamp(vphi, -1e6, 1e6);

    return vec3(vr, vtheta, vphi);
}
vec3 vel_spherical_to_cartesian(vec3 p_sph, vec3 v_sph) {
    float r = p_sph.x;
    float theta = p_sph.y;
    float phi = p_sph.z;

    float vr = v_sph.x;
    float vtheta = v_sph.y;
    float vphi = v_sph.z;

    float st = sin(theta);
    float ct = cos(theta);
    float sp = sin(phi);
    float cp = cos(phi);

    float vx = vr * st * cp + r * vtheta * ct * cp - r * st * vphi * sp;
    float vy = vr * ct      - r * vtheta * st;
    float vz = vr * st * sp + r * vtheta * ct * sp + r * st * vphi * cp;

    // Safety: clamp extreme values
    vx = clamp(vx, -1e6, 1e6);
    vy = clamp(vy, -1e6, 1e6);
    vz = clamp(vz, -1e6, 1e6);

    return vec3(vx, vy, vz);
}
vec2 directionToSpherical(vec3 direction) {
    vec3 d = normalize(direction);
    float theta = atan(d.z, d.x);
    float phi = asin(d.y);

    float u = (theta + PI) / (2.0 * PI);
    float v = (phi + PI * 0.5) / PI;

    return vec2(u, v);
}