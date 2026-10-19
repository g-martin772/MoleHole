#ifndef DISK_RADIANCE_GLSL
#define DISK_RADIANCE_GLSL

// Luminance of the hottest unshifted gas: one global calibration (like a fixed exposure) that puts the disk
// near the brightness of the pre-HDR renderer, so relative brightness and beaming stay physical.
const float DISK_REFERENCE_LUMINANCE = 0.25;

// hc / (lambda k_B) for 550 nm [K]
const float DISK_BAND_X = 26165.0;

float diskExpm1(float x) {
    return x < 0.02 ? x * (1.0 + 0.5 * x * (1.0 + x / 3.0)) : exp(x) - 1.0;
}

// Brightness of gas at observed temperature tObs relative to gas at `peak`: T^4 (bolometric), or the Planck function
// at 550 nm (visible), which I_nu / nu^3 invariance gives as B_nu(g T) at the fixed observed frequency.
float diskBrightnessRatio(float peak, float tObs) {
    if (u_diskBand < 0.5) {
        float ratio = tObs / peak;
        return (ratio * ratio) * (ratio * ratio);
    }
    float a = min(DISK_BAND_X / max(tObs, 1.0), 80.0);
    return diskExpm1(DISK_BAND_X / peak) / diskExpm1(a);
}

// Observed radiance of thermal gas at emitted temperature T seen with frequency ratio g, calibrated so that gas at
// `peak` with g = 1 shows DISK_REFERENCE_LUMINANCE. Colour from the blackbody LUT at the observed temperature g T;
// brightness from I ∝ T_obs^4 (I_nu / nu^3 is invariant along a ray) or the 550 nm Planck function (u_diskBand). The display temperature rescales every
// temperature by one factor for the colour lookup only, so ratios and shifts stay physical.
vec3 diskBlackbodyRadiance(float peak, float temperature, float g) {
    peak = max(peak, 1e-6);
    float displayScale = u_diskDisplayTemperature > 0.0 ? u_diskDisplayTemperature / peak : 1.0;
    vec3 lut = getBlackbodyColorLUT(g * temperature * displayScale, 1.0);
    float luminance = dot(lut, vec3(0.2126, 0.7152, 0.0722));
    vec3 chroma = luminance > 1e-6 ? lut / luminance : vec3(0.0);
    return u_diskGain * DISK_REFERENCE_LUMINANCE * chroma * diskBrightnessRatio(peak, g * temperature);
}

#endif
