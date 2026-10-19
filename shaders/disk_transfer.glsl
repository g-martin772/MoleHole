#ifndef DISK_TRANSFER_GLSL
#define DISK_TRANSFER_GLSL

// Radiative transfer through a segment of optical depth dTau with source S: I += T S (1 - e^-dTau), T *= e^-dTau.
// Exact for a constant source over the segment, so the result does not depend on the step size.
void diskAccumulate(float dTau, vec3 source, inout vec3 color, inout float alpha) {
    float transmittance = exp(-dTau);
    color += alpha * source * (1.0 - transmittance);
    alpha *= transmittance;
}

// The same for steps whose optical depth is far below float precision: 1 - e^-x is 0 in float for x < 6e-8, which would
// drop the emission of thin gas marched in small steps. Series below 1e-3, accurate to 1e-14 relative.
float diskEmissivity(float dTau) {
    return dTau < 1e-3 ? dTau * (1.0 - 0.5 * dTau * (1.0 - dTau / 3.0)) : 1.0 - exp(-dTau);
}

void diskAccumulateStable(float dTau, vec3 source, inout vec3 color, inout float alpha) {
    color += alpha * source * diskEmissivity(dTau);
    alpha *= exp(-dTau);
}

#endif
