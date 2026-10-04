#version 450

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D u_raytracedImage;
layout(set = 0, binding = 1) uniform sampler2D u_bloomImage;
layout(set = 0, binding = 2) uniform sampler2D u_lensFlareImage;

layout(push_constant) uniform PushConstants
{
    int u_fxaaEnabled;
    int u_bloomEnabled;
    float u_bloomIntensity;
    int u_bloomDebug; // 0=normal, 1=show bloom only, 2=show extraction only
    int u_lensFlareEnabled;
    float u_lensFlareIntensity;
    float rt_w;
    float rt_h;
};

const float FXAA_SPAN_MAX = 8.0;
const float FXAA_REDUCE_MUL = 1.0 / 8.0;
const float FXAA_REDUCE_MIN = 1.0 / 128.0;

vec3 FxaaPixelShader(vec2 uv, sampler2D tex, vec2 rcpFrame) {
    vec3 rgbNW = texture(tex, uv + vec2(-1.0, -1.0) * rcpFrame).rgb;
    vec3 rgbNE = texture(tex, uv + vec2(1.0, -1.0) * rcpFrame).rgb;
    vec3 rgbSW = texture(tex, uv + vec2(-1.0, 1.0) * rcpFrame).rgb;
    vec3 rgbSE = texture(tex, uv + vec2(1.0, 1.0) * rcpFrame).rgb;
    vec3 rgbM  = texture(tex, uv).rgb;

    vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM, luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-FXAA_SPAN_MAX), vec2(FXAA_SPAN_MAX)) * rcpFrame;

    vec3 rgbA = 0.5 * (
        texture(tex, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture(tex, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(tex, uv + dir * -0.5).rgb +
        texture(tex, uv + dir * 0.5).rgb);

    float lumaB = dot(rgbB, luma);
    if (lumaB < lumaMin || lumaB > lumaMax) {
        return rgbA;
    }
    return rgbB;
}

void main() {
    gl_FragDepth = texture(u_raytracedImage, TexCoord).a;

    vec3 color;
    if (u_fxaaEnabled == 1) {
        color = FxaaPixelShader(TexCoord, u_raytracedImage, vec2(1.0 / rt_w, 1.0 / rt_h));
    } else {
        color = texture(u_raytracedImage, TexCoord).rgb;
    }

    vec3 bloom = texture(u_bloomImage, TexCoord).rgb;
    vec3 flare = texture(u_lensFlareImage, TexCoord).rgb;

    if (u_bloomDebug == 1) {
        FragColor = vec4(bloom, 1.0);
        return;
    }
    if (u_bloomDebug == 2) {
        FragColor = vec4(bloom, 1.0);
        return;
    }

    if (u_bloomEnabled == 1) {
        color += bloom * u_bloomIntensity;
    }
    if (u_lensFlareEnabled == 1) {
        color += flare * u_lensFlareIntensity;
    }

    FragColor = vec4(color, 1.0);
}
