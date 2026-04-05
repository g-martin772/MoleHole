uniform int u_metric_type = 0; // 0 -> schwarschild; 1 -> kerr; 2 -> reissner-nordström; 3 ->  kerr-newman

const float G = 1.0f;
const float c = 1.0f;
const float EPSILON0 = 8.854187817e-12f;
const float PI = 3.14159265358979323846f;

// ------------------------------------------------------------------------------------------------------------
// Metric computation
// ------------------------------------------------------------------------------------------------------------
mat4 compute_schwarzschild_metric(float t, float r, float theta, float phi, float M) {
    float rs = 2.0f * G * M / pow(c, 2);
    float r2 = pow(r, 2);
    float sin2theta = pow(sin(theta), 2);

    mat4 g;
    g[0][0] = -(1 - (rs / r));
    g[1][1] = 1 / (1 - (rs / r));
    g[2][2] = r2;
    g[3][3] = r2 * sin2theta;

    return g;
}

mat4 compute_kerr_metric(float t, float r, float theta, float phi, float M, float a) {
    float rs = 2.0f * G * M / pow(c, 2);
    float r2 = pow(r, 2);
    float a2 = pow(a, 2);
    float sin2theta = pow(sin(theta), 2);
    float cos2theta = pow(cos(theta), 2);
    float Sigma = r2 + a2 * cos2theta;
    float Delta = r2 - rs * r + a2;

    mat4 g;
    g[0][0] = -(1 - (rs * r / Sigma));
    g[0][3] = -((rs * a * r * sin2theta) / Sigma);
    g[3][0] = g[0][3];
    g[1][1] = Sigma / Delta;
    g[2][2] = Sigma;
    g[3][3] = (sin2theta / Sigma) * (pow(r2 + a2, 2) - a2 * sin2theta * (r2 - rs * r + a2));

    return g;
}

mat4 compute_reissner_nordstrom_metric(float t, float r, float theta, float phi, float M, float Q) {
    float rs = 2.0f * G * M / pow(c, 2);
    float rq2 = (G * pow(Q, 2)) / (4.0f * PI * EPSILON0 * pow(c, 4));
    float r2 = pow(r, 2);
    float sin2theta = pow(sin(theta), 2);

    mat4 g;
    g[0][0] = -(1 - (rs / r) + (rq2 / r2));
    g[1][1] = 1 / (1 - (rs / r) + (rq2 / r2));
    g[2][2] = r2;
    g[3][3] = r2 * sin2theta;

    return g;
}

mat4 compute_kerr_newman_metric(float t, float r, float theta, float phi, float M, float a, float Q) {
    float Sigma = a;
    float Delta = a;

    mat4 g;
    g[0][0] = a;

    return g;
}

// spherical coordinates are used for all metrics
mat4 compute_metric(float t, float r, float theta, float phi, float M, float a, float Q) {
    if (u_metric_type == 0) { // non-rotating, uncharged
        return compute_schwarzschild_metric(t, r, theta, phi, M);
    }
    else if (u_metric_type == 1) { // rotating, uncharged
        return compute_kerr_metric(t, r, theta, phi, M, a);
    }
    else if (u_metric_type == 2) { // non-rotating, charged
        return compute_reissner_nordstrom_metric(t, r, theta, phi, M, Q);
    }
    else if (u_metric_type == 3) { // rotating, charged
        return compute_kerr_newman_metric(t, r, theta, phi, M, a, Q);
    }
    else {
        return mat4(1.0);
    }
}

mat4 compute_inv_metric(mat4 g) {
    return inverse(g);
}

mat4[4] compute_d_metric(float t, float r, float theta, float phi, float M, float a, float Q) {
    mat4[4] d_g;
    d_g[0] = (compute_metric(t + EPSILON, r, theta, phi, M, a, Q) - compute_metric(t - EPSILON, r, theta, phi, M, a, Q)) / (2.0f * EPSILON);
    d_g[1] = (compute_metric(t, r  + EPSILON, theta, phi, M, a, Q) - compute_metric(t, r - EPSILON, theta, phi, M, a, Q)) / (2.0f * EPSILON);
    d_g[2] = (compute_metric(t, r, theta + EPSILON, phi, M, a, Q) - compute_metric(t, r, theta - EPSILON, phi, M, a, Q)) / (2.0f * EPSILON);
    d_g[3] = (compute_metric(t, r, theta, phi + EPSILON, M, a, Q) - compute_metric(t, r, theta, phi - EPSILON, M, a, Q)) / (2.0f * EPSILON);

    return d_g;
}

// ------------------------------------------------------------------------------------------------------------
// Section Geodesic Equation
// ------------------------------------------------------------------------------------------------------------
// general geodesic equation for the schwarzschild case
void geodesic_equation(vec4 pos, vec4 vel, out vec4 accel, float M) {
    mat4 g = compute_metric(pos[0], pos[1], pos[2], pos[3], M, 0.0f, 0.0f);
    mat4 g_inv = compute_inv_metric(g);
    mat4[4] d_g = compute_d_metric(pos[0], pos[1], pos[2], pos[3]);

    for (int mu = 0; mu < 4; mu++) {
        for (int nu = 0; nu < 4; nu++) {
            for (int sigma = 0; sigma < 4; sigma++) {
                for (int rho = 0; rho < 4; rho++) {
                    accel[mu] -= 0.5f * g_inv[mu][rho] * (d_g[nu][rho][sigma] + d_g[rho][sigma][nu] - d_g[sigma][nu][rho]) * vel[nu] * vel[sigma];
                }
            }
        }
    }
}

// general geodesic equation for the kerr case
void geodesic_equation(vec4 pos, vec4 vel, out vec4 accel, float M, float a) {
    mat4 g = compute_metric(pos[0], pos[1], pos[2], pos[3], M, a, 0.0f);
    mat4 g_inv = compute_inv_metric(g);
    mat4[4] d_g = compute_d_metric(pos[0], pos[1], pos[2], pos[3]);

    for (int mu = 0; mu < 4; mu++) {
        for (int nu = 0; nu < 4; nu++) {
            for (int sigma = 0; sigma < 4; sigma++) {
                for (int rho = 0; rho < 4; rho++) {
                    accel[mu] -= 0.5f * g_inv[mu][rho] * (d_g[nu][rho][sigma] + d_g[rho][sigma][nu] - d_g[sigma][nu][rho]) * vel[nu] * vel[sigma];
                }
            }
        }
    }
}

// general geodesic equation for the reissner-nordström case
void geodesic_equation(vec4 pos, vec4 vel, out vec4 accel, float M, float Q) {
    mat4 g = compute_metric(pos[0], pos[1], pos[2], pos[3], M, 0.0f, Q);
    mat4 g_inv = compute_inv_metric(g);
    mat4[4] d_g = compute_d_metric(pos[0], pos[1], pos[2], pos[3]);

    for (int mu = 0; mu < 4; mu++) {
        for (int nu = 0; nu < 4; nu++) {
            for (int sigma = 0; sigma < 4; sigma++) {
                for (int rho = 0; rho < 4; rho++) {
                    accel[mu] -= 0.5f * g_inv[mu][rho] * (d_g[nu][rho][sigma] + d_g[rho][sigma][nu] - d_g[sigma][nu][rho]) * vel[nu] * vel[sigma];
                }
            }
        }
    }
}

// general geodesic equation for the kerr-newman case
void geodesic_equation(vec4 pos, vec4 vel, out vec4 accel, float M, float a, float Q) {
    mat4 g = compute_metric(pos[0], pos[1], pos[2], pos[3], M, a, Q);
    mat4 g_inv = compute_inv_metric(g);
    mat4[4] d_g = compute_d_metric(pos[0], pos[1], pos[2], pos[3]);

    for (int mu = 0; mu < 4; mu++) {
        for (int nu = 0; nu < 4; nu++) {
            for (int sigma = 0; sigma < 4; sigma++) {
                for (int rho = 0; rho < 4; rho++) {
                    accel[mu] -= 0.5f * g_inv[mu][rho] * (d_g[nu][rho][sigma] + d_g[rho][sigma][nu] - d_g[sigma][nu][rho]) * vel[nu] * vel[sigma];
                }
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------------------
// Section Numerical Integration
// ------------------------------------------------------------------------------------------------------------
vec4 rk4_step(vec4 p, float dt) {
    float k1, k2, k3, k4;
    vec4 a;
    vec4 v;
    float M = 1.0f;


    p += dt;
    geodesic_equation(p, v, k1, M);
    geodesic_equation(p + 0.5f * dt, v + 0.5f * k1, k2, M);
    geodesic_equation(p + 0.5f * dt, v + 0.5f * k2, k3, M);
    geodesic_equation(p + dt, v + dt * k3, k4, M);

    return p + (dt / 6.0f) * (k1 + 2.0f * k2 + 2.0f * k3 + k4);
}