const float BOLTZMANN = 5.670374419e-8;

// ------------------------------------------------------------------------------------------------------------
// Section Event Horizon
// ------------------------------------------------------------------------------------------------------------
float calculateEventHorizonRadius(float mass) {
    return 2.0f * mass;
}

// ------------------------------------------------------------------------------------------------------------
// Section Influence Radius
// ------------------------------------------------------------------------------------------------------------
float calculateInfluenceRadius(float r_s) {
    return 10.0f * r_s;
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
    float theta = acos(pos.y / r);
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
    float x = p_cart[0];
    float y = p_cart[1];
    float z = p_cart[2];
    float vx = v_cart[0];
    float vy = v_cart[1];
    float vz = v_cart[2];

    float r = length(p_cart);
    float rho = sqrt(x * x + z * z);

    // Safety check for poles
    if (rho < 0.0001) rho = 0.0001;

    float vr = (x * vx + y * vy + z * vz) / r;
    float vtheta = (y * (x * vx + z * vz) - rho * rho * vy) / (r * r * rho);
    float vphi = (x * vz - z * vx) / (rho * rho);

    return vec3(vr, vtheta, vphi);
}
vec3 vel_spherical_to_cartesian(vec3 p_sph, vec3 v_sph) {
    float r = p_sph[0];
    float theta = p_sph[1];
    float phi = p_sph[2];

    float vr = v_sph[0];
    float vtheta = v_sph[1];
    float vphi = v_sph[2];

    float st = sin(theta);
    float ct = cos(theta);
    float sp = sin(phi);
    float cp = cos(phi);

    float vx = vr * st * cp + r * vtheta * ct * cp - r * st * vphi * sp;
    float vy = vr * ct      - r * vtheta * st;
    float vz = vr * st * sp + r * vtheta * ct * sp + r * st * vphi * cp;

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