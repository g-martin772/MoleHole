#ifndef REAL_GLSL
#define REAL_GLSL
#ifdef GPP_PRECISION_F64
#extension GL_ARB_gpu_shader_fp64 : require
#define real double
#define rvec3 dvec3
#define rvec4 dvec4
#define rmat4 dmat4

const real R_FD_EPS = 1e-6lf;
const real R_PI = 3.14159265358979323846264338327950288lf;

real rsin_poly(real x) {
    real x2 = x * x;
    real term = x;
    real sum = x;
    for (int i = 1; i < 12; i++) {
        term *= -x2 / real((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

real rcos_poly(real x) {
    real x2 = x * x;
    real term = 1.0lf;
    real sum = 1.0lf;
    for (int i = 1; i < 12; i++) {
        term *= -x2 / real((2 * i - 1) * (2 * i));
        sum += term;
    }
    return sum;
}

void rsincos(real x, out real s, out real c) {
    const real HALF_PI_HI = 1.5707963267341256e+00lf;
    const real HALF_PI_LO = 6.0771005065061922e-11lf;
    real n = floor(x / (R_PI * 0.5lf) + 0.5lf);
    real r = (x - n * HALF_PI_HI) - n * HALF_PI_LO;
    real sr = rsin_poly(r);
    real cr = rcos_poly(r);
    int q = int(mod(n, 4.0lf));
    if (q < 0) q += 4;
    if (q == 0) { s = sr; c = cr; }
    else if (q == 1) { s = cr; c = -sr; }
    else if (q == 2) { s = -sr; c = -cr; }
    else { s = -cr; c = sr; }
}

real rsin(real x) { real s, c; rsincos(x, s, c); return s; }
real rcos(real x) { real s, c; rsincos(x, s, c); return c; }

real racos(real x) {
    x = clamp(x, -1.0lf, 1.0lf);
    real t = real(acos(float(x)));
    for (int i = 0; i < 2; i++) {
        real s, c;
        rsincos(t, s, c);
        t += (c - x) / max(s, 1e-12lf);
    }
    return t;
}

real ratan2(real y, real x) {
    real t = real(atan(float(y), float(x)));
    for (int i = 0; i < 2; i++) {
        real s, c;
        rsincos(t, s, c);
        t += (y * c - x * s) / (x * c + y * s);
    }
    return t;
}
#else
#define real float
#define rvec3 vec3
#define rvec4 vec4
#define rmat4 mat4

const real R_FD_EPS = 5e-5;
const real R_PI = 3.14159265358979323846;
#define rsin sin
#define rcos cos
#define racos acos
#define ratan2 atan
#endif

real rsq(real x) { return x * x; }

void rsincos_pair(real x, out real s, out real c) {
#ifdef GPP_PRECISION_F64
    rsincos(x, s, c);
#else
    s = sin(x);
    c = cos(x);
#endif
}
#endif
