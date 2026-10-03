#version 450

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;

layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 0, std140) uniform MeshCameraBlock
{
    mat4 u_view;
    mat4 u_projection;
    vec3 u_cameraPos; float _pad0;
    vec3 u_lightDir; float _pad1;
};

layout(set = 0, binding = 1) uniform sampler2D u_baseColorTexture;

layout(push_constant) uniform PushConstants
{
    mat4 u_model;
    vec4 u_baseColorFactor;
    float u_metallicFactor;
    float u_roughnessFactor;
    int u_hasBaseColorTexture;
    float _pad2;
};

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec4 baseColor = u_baseColorFactor;
    if (u_hasBaseColorTexture == 1) {
        baseColor *= texture(u_baseColorTexture, TexCoord);
    }

    vec3 N = normalize(Normal);
    vec3 V = normalize(u_cameraPos - FragPos);
    vec3 L = normalize(u_lightDir);
    vec3 H = normalize(V + L);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, baseColor.rgb, u_metallicFactor);

    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float NDF = DistributionGGX(N, H, u_roughnessFactor);
    float G = GeometrySmith(N, V, L, u_roughnessFactor);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - u_metallicFactor;

    float NdotL = max(dot(N, L), 0.0);
    vec3 radiance = vec3(1.0);

    vec3 Lo = (kD * baseColor.rgb / PI + specular) * radiance * NdotL;

    vec3 ambient = vec3(0.03) * baseColor.rgb;
    vec3 color = ambient + Lo;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, baseColor.a);
}
