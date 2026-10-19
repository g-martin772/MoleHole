// Paczynski-Wiita point mass plus the alpha-disk relaxation of AccretionDisk; the maths is mirrored in
// src/simulation/accretion_disk.cppm (DiskKernel::PostMacro). User parameters: 0 alpha, 1 aspect ratio H/R, 2 inner
// radius, 3-5 disk normal, 6 temperature coefficient, 7 tidal truncation radius, 8 its strength, 9 radial damping boost of non-circular gas.

#define PF_HAS_POST_MACRO

float pfPwDistance(float r, float rs)
{
    return max(r - rs, 0.001 * rs);
}

vec3 pfAcceleration(vec3 x, vec3 v, ForceParams f)
{
    float r = max(length(x), 1e-30);
    float d = pfPwDistance(r, f.rs);
    return -f.mu * x / (r * d * d);
}

float pfStableStep(vec3 x, vec3 v, float eta, ForceParams f)
{
    float speed = length(v) + 1e-30;
    float r = max(length(x), 1e-30);
    float d = pfPwDistance(r, f.rs);
    return eta * 6.28318530717958647692 * min(sqrt(r * d * d / f.mu), d / speed);
}

float pfPotential(vec3 x, ForceParams f)
{
    return -f.mu / pfPwDistance(length(x), f.rs);
}

void pfPostMacro(inout Particle p, float dt, ForceParams f)
{
    float alpha = f.user[0];
    float aspect = f.user[1];
    float inner = f.user[2];
    vec3 normal = vec3(f.user[3], f.user[4], f.user[5]);
    float height = dot(p.pos, normal);
    vec3 planar = p.pos - height * normal;
    float R = length(planar);
    if (!(R > inner)) return;
    vec3 radial = planar / R;
    vec3 azimuth = cross(normal, radial);
    float d = pfPwDistance(R, f.rs);
    float omega = sqrt(f.mu / R) / d;
    float rate = alpha * aspect * aspect * omega;
    float keplerSpeed = R * omega;
    float vr = dot(p.vel, radial);
    float vphi = dot(p.vel, azimuth);
    float vz = dot(p.vel, normal);

    float shock = f.user[9];
    float vrRate = rate;
    float circular = 1.0;
    {
        float dv = vphi - keplerSpeed;
        float e2 = (vr * vr + dv * dv) / (keplerSpeed * keplerSpeed);
        float t = clamp((e2 - 0.04) / (0.25 - 0.04), 0.0, 1.0);
        float s = t * t * (3.0 - 2.0 * t);
        if (shock > 0.0) vrRate = rate * (1.0 + shock * s);
        circular = 1.0 - s;
    }
    vr *= exp(-vrRate * dt);

    float edge = max(1.0 - sqrt(inner / R), 0.04);
    float drift = 1.5 * rate * R / edge;
    float gradient = sqrt(f.mu * R) * (0.5 * R - 1.5 * f.rs) / (d * d);
    float l = R * vphi;
    float loss = drift * gradient * dt;
    l = l >= 0.0 ? max(l - loss, 0.0) : min(l + loss, 0.0);
    float truncation = f.user[7];
    if (truncation > 0.0 && R > truncation)
    {
        float x = R / truncation - 1.0;
        l *= exp(-f.user[8] * omega * x * x * circular * dt);
    }
    vphi = l / R;

    uvec4 bits = pfPhilox(uvec4(p.serial, floatBitsToUint(p.age), p.species, 0u), uvec2(0xD15C0001u, floatBitsToUint(dt)));
    float xi = pfNormal(pfUnit(bits.x), pfUnit(bits.y)).x;
    if (alpha > 0.0)
    {
        float keep = exp(-omega * dt);
        vz = vz * keep + aspect * keplerSpeed * sqrt(max(1.0 - keep * keep, 0.0)) * xi;
    }

    p.vel = vr * radial + vphi * azimuth + vz * normal;

    float coefficient = f.user[6];
    if (coefficient > 0.0)
    {
        float fourth = coefficient * max(1.0 - sqrt(inner / R), 0.0) / (R * R * R);
        float effective = sqrt(sqrt(fourth));
        float relax = exp(-alpha * omega * dt);
        p.temperature = effective + (p.temperature - effective) * relax;
    }
}
