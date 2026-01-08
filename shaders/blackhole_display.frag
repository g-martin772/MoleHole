#version 460 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D u_raytracedImage;
uniform sampler2D u_bloomImage;
uniform int u_fxaaEnabled = 1;
uniform int u_bloomEnabled = 1;
uniform float u_bloomIntensity = 5.0;
uniform int u_bloomDebug = 0; // Debug mode: 0=normal, 1=show bloom only, 2=show extraction only

uniform float FXAA_SPAN_MAX = 8.0;
uniform float FXAA_REDUCE_MUL = 1.0/8.0;

uniform float rt_w = 1920.0;
uniform float rt_h = 1080.0;

#define FxaaInt2 ivec2
#define FxaaFloat2 vec2
#define FxaaTexLod0(t, p) textureLod(t, p, 0.0)
#define FxaaTexOff(t, p, o, r) textureLodOffset(t, p, 0.0, o)

vec3 FxaaPixelShader(
    vec4 posPos, // Output of FxaaVertexShader interpolated across screen.
    sampler2D tex, // Input texture.
    vec2 rcpFrame) // Constant {1.0/frameWidth, 1.0/frameHeight}.
{
/*---------------------------------------------------------*/
    #define FXAA_REDUCE_MIN   (1.0/128.0)
/*---------------------------------------------------------*/
    vec3 rgbNW = FxaaTexLod0(tex, posPos.zw).xyz;
    vec3 rgbNE = FxaaTexOff(tex, posPos.zw, FxaaInt2(1,0), rcpFrame.xy).xyz;
    vec3 rgbSW = FxaaTexOff(tex, posPos.zw, FxaaInt2(0,1), rcpFrame.xy).xyz;
    vec3 rgbSE = FxaaTexOff(tex, posPos.zw, FxaaInt2(1,1), rcpFrame.xy).xyz;
    vec3 rgbM  = FxaaTexLod0(tex, posPos.xy).xyz;
/*---------------------------------------------------------*/
    vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM,  luma);
/*---------------------------------------------------------*/
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
/*---------------------------------------------------------*/
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));
/*---------------------------------------------------------*/
    float dirReduce = max(
        (lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL),
        FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0/(min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = min(FxaaFloat2( FXAA_SPAN_MAX,  FXAA_SPAN_MAX),
              max(FxaaFloat2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX),
                  dir * rcpDirMin)) * rcpFrame.xy;
/*--------------------------------------------------------*/
    vec3 rgbA = (1.0/2.0) * (
    FxaaTexLod0(tex, posPos.xy + dir * (1.0/3.0 - 0.5)).xyz +
    FxaaTexLod0(tex, posPos.xy + dir * (2.0/3.0 - 0.5)).xyz);
    vec3 rgbB = rgbA * (1.0/2.0) + (1.0/4.0) * (
    FxaaTexLod0(tex, posPos.xy + dir * (0.0/3.0 - 0.5)).xyz +
    FxaaTexLod0(tex, posPos.xy + dir * (3.0/3.0 - 0.5)).xyz);
    float lumaB = dot(rgbB, luma);
    if((lumaB < lumaMin) || (lumaB > lumaMax)) return rgbA;
    return rgbB; }

void main() {
    vec3 bloomColor = texture(u_bloomImage, TexCoord).rgb;

    if (u_bloomDebug == 1) {
        FragColor = vec4(bloomColor * u_bloomIntensity, 1.0);
    } else if (u_bloomDebug == 2) {
        FragColor = vec4(bloomColor * 10.0, 1.0);
    } else {
        vec3 color;
        if (u_fxaaEnabled == 1) {
            vec4 posPos;
            posPos.xy = TexCoord;
            vec2 rcpFrame = vec2(1.0/rt_w, 1.0/rt_h);
            posPos.zw = TexCoord - (rcpFrame * (0.5 + 1.0/128.0));
            color = FxaaPixelShader(posPos, u_raytracedImage, rcpFrame);
        } else {
            color = texture(u_raytracedImage, TexCoord).rgb;
        }

        if (u_bloomEnabled == 1) {
            color += bloomColor * u_bloomIntensity;
        }

        FragColor = vec4(color, 1.0);
    }
}
