const float PI = 3.1415926535;
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
    return 7.0f * r_s;
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
    float rho = sqrt(pow(pos.x, 2) + pow(pos.y, 2) + pow(pos.z, 2));
    float theta = atan(pos.z, pos.x);
    float phi = asin(pos.y / rho);
    return vec3(rho, theta, phi);
}
vec2 directionToSpherical(vec3 direction) {
    vec3 d = normalize(direction);
    float theta = atan(d.z, d.x);
    float phi = asin(d.y);

    float u = (theta + PI) / (2.0 * PI);
    float v = (phi + PI * 0.5) / PI;

    return vec2(u, v);
}