const float G = 1.0f;
const float c = 1.0f;

rmat4 compute_kerr_newman_metric(real r, real theta, real M, real a, real Q) {
    real rs = 2.0 * M;
    real rq2 = rsq(Q * M);
    real r2 = r * r;
    real a2 = a * a;
    real safe_theta = clamp(theta, real(EPSILON), real(R_PI) - real(EPSILON));
    real sin2theta = rsq(rsin(safe_theta));
    real cos2theta = rsq(rcos(safe_theta));
    real Sigma = r2 + a2 * cos2theta;
    real Delta = r2 - rs * r + a2 + rq2;

    rmat4 g = rmat4(0.0);
    g[0][0] = -(Delta - a2 * sin2theta) / Sigma;
    g[0][3] = -a * sin2theta * (r2 + a2 - Delta) / Sigma;
    g[3][0] = g[0][3];
    g[1][1] = Sigma / Delta;
    g[2][2] = Sigma;
    g[3][3] = (rsq(r2 + a2) - a2 * Delta * sin2theta) * sin2theta / Sigma;
    return g;
}

// 0 Schwarzschild, 1 Kerr, 2 Reissner-Nordstrom, 3 Kerr-Newman.
void select_parameters(inout real a, inout real Q) {
    if (u_metric_type == 0) { a = 0.0; Q = 0.0; }
    else if (u_metric_type == 1) { Q = 0.0; }
    else if (u_metric_type == 2) { a = 0.0; }
}

rmat4 compute_metric(real t, real r, real theta, real phi, real M, real a, real Q) {
    select_parameters(a, Q);
    return compute_kerr_newman_metric(r, theta, M, a, Q);
}

rmat4 compute_inv_metric(rmat4 g) {
    return inverse(g);
}

rmat4[4] compute_d_metric(real t, real r, real theta, real phi, real M, real a, real Q) {
    rmat4[4] d_g;
    d_g[0] = rmat4(0.0);
    d_g[3] = rmat4(0.0);
    d_g[1] = (compute_metric(t, r + R_FD_EPS, theta, phi, M, a, Q) - compute_metric(t, r - R_FD_EPS, theta, phi, M, a, Q)) / (2.0 * R_FD_EPS);
    d_g[2] = (compute_metric(t, r, theta + R_FD_EPS, phi, M, a, Q) - compute_metric(t, r, theta - R_FD_EPS, phi, M, a, Q)) / (2.0 * R_FD_EPS);
    return d_g;
}

// Geodesic equation d^2x^mu/dlambda^2 = -Gamma^mu_{nu sigma} v^nu v^sigma.
// The metric is stationary and axisymmetric,
// so the t and phi derivatives vanish?
void geodesic_equation(rvec4 pos, rvec4 vel, out rvec4 accel, real M, real a, real Q) {
    rmat4 g = compute_metric(pos[0], pos[1], pos[2], pos[3], M, a, Q);
    rmat4 g_inv = compute_inv_metric(g);
    rmat4[4] d_g = compute_d_metric(pos[0], pos[1], pos[2], pos[3], M, a, Q);

    rvec4 Gamma = rvec4(0.0);
    for (int rho = 0; rho < 4; rho++) {
        for (int nu = 0; nu < 4; nu++) {
            for (int sigma = 0; sigma < 4; sigma++) {
                Gamma[rho] += 0.5 * (d_g[nu][rho][sigma] + d_g[sigma][nu][rho] - d_g[rho][sigma][nu]) * vel[nu] * vel[sigma];
            }
        }
    }

    accel = rvec4(0.0);
    for (int mu = 0; mu < 4; mu++) {
        for (int rho = 0; rho < 4; rho++) {
            accel[mu] -= g_inv[mu][rho] * Gamma[rho];
        }
    }
}

void rk4_step(inout rvec4 p, inout rvec4 vel, real dt, real M, real a, real Q) {
    rvec4 k1_v, k2_v, k3_v, k4_v;
    rvec4 k1_p, k2_p, k3_p, k4_p;

    geodesic_equation(p, vel, k1_v, M, a, Q);
    k1_p = vel;

    geodesic_equation(p + 0.5 * dt * k1_p, vel + 0.5 * dt * k1_v, k2_v, M, a, Q);
    k2_p = vel + 0.5 * dt * k1_v;

    geodesic_equation(p + 0.5 * dt * k2_p, vel + 0.5 * dt * k2_v, k3_v, M, a, Q);
    k3_p = vel + 0.5 * dt * k2_v;

    geodesic_equation(p + dt * k3_p, vel + dt * k3_v, k4_v, M, a, Q);
    k4_p = vel + dt * k3_v;

    p += (dt / 6.0) * (k1_p + 2.0 * k2_p + 2.0 * k3_p + k4_p);
    vel += (dt / 6.0) * (k1_v + 2.0 * k2_v + 2.0 * k3_v + k4_v);
}
