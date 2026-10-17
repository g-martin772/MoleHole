// Null geodesics of the Kerr–Newman family in Boyer–Lindquist coordinates (G = c = 1).
// Schwarzschild (a = Q = 0), Kerr (Q = 0) and Reissner–Nordström (a = 0) are special cases; the host
// zeroes a / Q according to the selected metric and precomputes r+ and the ISCO (src/rendering/metric.cppm).
// The integration runs in `real` (double with GPP_PRECISION_F64). Background and sources:
// docs/notes/metric_rendering.md

struct Spacetime {
    real M;
    real a;
    real Q;
    real rPlus;
    real rIsco;
};

Spacetime spacetimeOf(BlackHoleData bh) {
    Spacetime st;
    st.M = real(bh.mass);
    st.a = real(bh.spin) * st.M;
    st.Q = real(bh.charge) * st.M;
    st.rPlus = real(bh.horizonRadius);
    st.rIsco = real(bh.iscoRadius);
    return st;
}

real safeSin(real s) {
    return abs(s) < real(1e-4) ? (s < real(0.0) ? real(-1e-4) : real(1e-4)) : s;
}

// Local frame of a hole: columns X, Y, Z with Z along the spin axis, right-handed so that +phi is the
// direction of rotation. world = F * local, local = transpose(F) * world.
mat3 spinFrame(vec3 spinAxis) {
    vec3 z = normalize(spinAxis);
    vec3 helper = abs(z.y) < 0.99f ? vec3(0.0f, 1.0f, 0.0f) : vec3(1.0f, 0.0f, 0.0f);
    vec3 x = normalize(cross(helper, z));
    vec3 y = cross(z, x);
    return mat3(x, y, z);
}

// Boyer–Lindquist (r, theta, phi) <-> local Cartesian. For a != 0 these are oblate spheroidal coordinates;
// plain spherical coordinates would put the camera at the wrong r.
rvec3 cartesianToBL(rvec3 p, real a) {
    real a2 = a * a;
    real w = dot(p, p) - a2;
    real r2 = real(0.5) * (w + sqrt(w * w + real(4.0) * a2 * p.z * p.z));
    real r = sqrt(max(r2, real(1e-12)));
    real theta = racos(clamp(p.z / r, real(-1.0), real(1.0)));
    real phi = ratan2(p.y, p.x);
    return rvec3(r, theta, phi);
}

vec3 blToCartesian(real r, real theta, real phi, real a) {
    real s, ct, sp, cp;
    rsincos_pair(theta, s, ct);
    rsincos_pair(phi, sp, cp);
    real rho = sqrt(r * r + a * a);
    return vec3(rvec3(rho * s * cp, rho * s * sp, r * ct));
}

// Orthonormal directions of increasing r, theta, phi (they are mutually orthogonal in flat space).
void blBasis(real r, real theta, real phi, real a, out vec3 er, out vec3 eTheta, out vec3 ePhi) {
    real s, ct, sp, cp;
    rsincos_pair(theta, s, ct);
    rsincos_pair(phi, sp, cp);
    real rho = sqrt(r * r + a * a);
    er = normalize(vec3(rvec3(r / rho * s * cp, r / rho * s * sp, ct)));
    eTheta = normalize(vec3(rvec3(rho * ct * cp, rho * ct * sp, -r * s)));
    ePhi = vec3(rvec3(-sp, cp, real(0.0)));
}

// Velocity of a Boyer–Lindquist trajectory expressed in the hole's local Cartesian frame.
vec3 blVelocityToCartesian(real r, real theta, real phi, real a, real dr, real dtheta, real dphi) {
    real s, ct, sp, cp;
    rsincos_pair(theta, s, ct);
    rsincos_pair(phi, sp, cp);
    real rho = sqrt(r * r + a * a);
    rvec3 dR = rvec3(r / rho * s * cp, r / rho * s * sp, ct);
    rvec3 dTheta = rvec3(rho * ct * cp, rho * ct * sp, -r * s);
    rvec3 dPhi = rvec3(-rho * s * sp, rho * s * cp, real(0.0));
    return vec3(dr * dR + dtheta * dTheta + dphi * dPhi);
}

// Covariant t/phi block of the metric at (r, theta); the rr and thetatheta parts are Sigma/Delta and Sigma.
void metricTPhi(Spacetime st, real r, real theta, out real gtt, out real gtphi, out real gphiphi) {
    real s, ct;
    rsincos_pair(theta, s, ct);
    s = safeSin(s);
    real a2 = st.a * st.a, s2 = s * s;
    real Sigma = r * r + a2 * ct * ct;
    real Delta = r * r - real(2.0) * st.M * r + a2 + st.Q * st.Q;
    real A = rsq(r * r + a2) - Delta * a2 * s2;
    gtt = -(Delta - a2 * s2) / Sigma;
    gtphi = -st.a * s2 * (r * r + a2 - Delta) / Sigma;
    gphiphi = A * s2 / Sigma;
}

// Lapse alpha and frame-dragging rate omega of the zero-angular-momentum observer (ZAMO).
void zamo(Spacetime st, real r, real theta, out real alpha, out real omega) {
    real s, ct;
    rsincos_pair(theta, s, ct);
    s = safeSin(s);
    real a2 = st.a * st.a;
    real Sigma = r * r + a2 * ct * ct;
    real Delta = r * r - real(2.0) * st.M * r + a2 + st.Q * st.Q;
    real A = rsq(r * r + a2) - Delta * a2 * s * s;
    alpha = sqrt(max(Sigma * Delta / A, real(0.0)));
    omega = st.a * (real(2.0) * st.M * r - st.Q * st.Q) / A;
}

// Hamiltonian H = K / (2 Sigma) in Carter's separated form with p_t = -E and p_phi = L conserved:
//   K = Delta p_r^2 + p_theta^2 + (L / sin(theta) - a E sin(theta))^2 - ((r^2 + a^2) E - a L)^2 / Delta
// State y = (r, theta, p_r, p_theta); phi is integrated alongside for the disk and the exit direction.
void geodesicRhs(Spacetime st, real E, real L, rvec4 y, out rvec4 dy, out real dphi, out real K) {
    real r = y.x, pr = y.z, pth = y.w;
    real a = st.a, a2 = a * a;
    real s, ct;
    rsincos_pair(y.y, s, ct);
    s = safeSin(s);

    real Sigma = r * r + a2 * ct * ct;
    real Delta = r * r - real(2.0) * st.M * r + a2 + st.Q * st.Q;
    real P = (r * r + a2) * E - a * L;
    real T = L / s - a * E * s;
    K = Delta * pr * pr + pth * pth + T * T - P * P / Delta;

    real dDelta = real(2.0) * r - real(2.0) * st.M;
    real dP = real(2.0) * r * E;
    real Kr = dDelta * pr * pr - (real(2.0) * P * dP * Delta - P * P * dDelta) / (Delta * Delta);
    real Ktheta = real(2.0) * T * (-L * ct / (s * s) - a * E * ct);
    real SigmaR = real(2.0) * r;
    real SigmaTheta = real(-2.0) * a2 * ct * s;

    dy.x = Delta * pr / Sigma;
    dy.y = pth / Sigma;
    dy.z = real(-0.5) * (Kr - K * SigmaR / Sigma) / Sigma;
    dy.w = real(-0.5) * (Ktheta - K * SigmaTheta / Sigma) / Sigma;
    dphi = (T / s + a * P / Delta) / Sigma;
}

// k1 / f1 are the derivatives at y, already evaluated by the caller to choose the step size.
void geodesicStep(Spacetime st, real E, real L, inout rvec4 y, inout real phi, real h, rvec4 k1, real f1) {
    rvec4 k2, k3, k4;
    real f2, f3, f4, K;
    geodesicRhs(st, E, L, y + real(0.5) * h * k1, k2, f2, K);
    geodesicRhs(st, E, L, y + real(0.5) * h * k2, k3, f3, K);
    geodesicRhs(st, E, L, y + h * k3, k4, f4, K);
    y += (h / real(6.0)) * (k1 + real(2.0) * k2 + real(2.0) * k3 + k4);
    phi += (h / real(6.0)) * (f1 + real(2.0) * f2 + real(2.0) * f3 + f4);
}

// Photon momentum seen by a ZAMO at (r, theta) travelling along the unit local direction n (components
// along e_r, e_theta, e_phi), normalised to unit energy in that frame. Exactly null by construction.
void launchPhoton(Spacetime st, real r, real theta, vec3 n, out real E, out real L, out real pr, out real pth) {
    real s, ct;
    rsincos_pair(theta, s, ct);
    s = safeSin(s);
    real a2 = st.a * st.a;
    real Sigma = r * r + a2 * ct * ct;
    real Delta = r * r - real(2.0) * st.M * r + a2 + st.Q * st.Q;
    real A = rsq(r * r + a2) - Delta * a2 * s * s;
    real alpha, omega;
    zamo(st, r, theta, alpha, omega);

    L = sqrt(A / Sigma) * abs(s) * real(n.z);
    E = alpha + omega * L;
    pr = sqrt(Sigma / Delta) * real(n.x);
    pth = sqrt(Sigma) * real(n.y);
}

// Angular velocity of a prograde circular equatorial orbit at radius r (see ProgradeOrbitalFrequency).
real keplerOmega(Spacetime st, real r) {
    real a = st.a;
    real df = (real(2.0) * st.Q * st.Q - real(2.0) * st.M * r) / (r * r * r);
    real dgtt = df;
    real dgtphi = -a * df;
    real dgphiphi = real(2.0) * r + a * a * df;
    return (-dgtphi + sqrt(max(dgtphi * dgtphi - dgtt * dgphiphi, real(0.0)))) / dgphiphi;
}

// Frequency ratio g = nu_observed / nu_emitted for disk gas on circular orbits, relative to the ZAMO the
// photon was launched from (unit energy there). gTotal contains gravitational shift, Doppler shift and
// frame dragging; alphaEmitter is the lapse at the emitter for splitting off the gravitational part.
void diskRedshift(Spacetime st, real r, real theta, real E, real L, out float gTotal, out float alphaEmitter) {
    real gtt, gtphi, gphiphi;
    metricTPhi(st, r, theta, gtt, gtphi, gphiphi);
    real alpha, omegaZamo;
    zamo(st, r, theta, alpha, omegaZamo);

    real omega = keplerOmega(st, r);
    real norm = -(gtt + real(2.0) * gtphi * omega + gphiphi * omega * omega);
    if (!(norm > real(1e-6))) {
        // No timelike circular orbit here (off-plane or too close); co-rotate with the local ZAMO instead.
        omega = omegaZamo;
        norm = -(gtt + real(2.0) * gtphi * omega + gphiphi * omega * omega);
    }
    real ut = inversesqrt(max(norm, real(1e-12)));
    gTotal = float(real(1.0) / max(ut * (E - omega * L), real(1e-6)));
    alphaEmitter = float(alpha);
}
