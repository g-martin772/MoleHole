// Narkowicz (2015) fit of the ACES reference rendering transform.
vec3 toneMapAces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// Reinhard et al. (2002) on luminance, keeping the hue.
vec3 toneMapReinhard(vec3 x) {
    float luminance = dot(x, vec3(0.2126, 0.7152, 0.0722));
    return x / (1.0 + luminance);
}

// The colour target is UNORM, so the display encoding is applied here.
vec3 linearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

vec3 displayColor(vec3 hdr, float exposure, int toneMap) {
    if (toneMap == 0) return hdr;
    vec3 exposed = max(hdr, vec3(0.0)) * exposure;
    return linearToSrgb(toneMap == 1 ? toneMapReinhard(exposed) : toneMapAces(exposed));
}
