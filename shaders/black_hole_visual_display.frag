#version 330 core

const float PI = 3.14159265359;
const float EPSILON = 0.0001;
const float INFINITY = 1000000.0;

out vec4 fragColor;

uniform vec2 resolution; // viewport resolution in pixels
uniform float mouseX;
uniform float mouseY;

uniform float time; // time elapsed in seconds
uniform samplerCube galaxy;
uniform sampler2D colorMap;

// Camera uniforms - replacing hardcoded camera logic
uniform vec3 cameraPos;
uniform vec3 cameraFront;
uniform vec3 cameraUp;
uniform vec3 cameraRight;
uniform float cameraFov;
uniform float cameraAspect;

// Legacy uniforms (kept for compatibility)
uniform float frontView = 0.0;
uniform float topView = 0.0;
uniform float cameraRoll = 0.0;

uniform float gravatationalLensing = 1.0;
uniform float renderBlackHole = 1.0;
uniform float mouseControl = 0.0;
uniform float fovScale = 1.0;

// Black hole physics uniforms
uniform float eventHorizonRadius = 2.0;  // Controllable event horizon radius

uniform float adiskEnabled = 1.0;
uniform float adiskParticle = 1.0;
uniform float adiskHeight = 0.2;
uniform float adiskLit = 0.5;
uniform float adiskDensityV = 1.0;
uniform float adiskDensityH = 1.0;
uniform float adiskNoiseScale = 1.0;
uniform float adiskNoiseLOD = 5.0;
uniform float adiskSpeed = 0.5;

///----
/// Simplex 3D Noise
/// by Ian McEwan, Ashima Arts
vec4 permute(vec4 x) { return mod(((x * 34.0) + 1.0) * x, 289.0); }
vec4 taylorInvSqrt(vec4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float snoise(vec3 v) {
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

    // First corner
    vec3 i = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);

    // Other corners
    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);

    //  x0 = x0 - 0. + 0.0 * C
    vec3 x1 = x0 - i1 + 1.0 * C.xxx;
    vec3 x2 = x0 - i2 + 2.0 * C.xxx;
    vec3 x3 = x0 - 1. + 3.0 * C.xxx;

    // Permutations
    i = mod(i, 289.0);
    vec4 p = permute(permute(permute(i.z + vec4(0.0, i1.z, i2.z, 1.0)) + i.y +
    vec4(0.0, i1.y, i2.y, 1.0)) +
    i.x + vec4(0.0, i1.x, i2.x, 1.0));

    // Gradients
    // ( N*N points uniformly over a square, mapped onto an octahedron.)
    float n_ = 1.0 / 7.0; // N=7
    vec3 ns = n_ * D.wyz - D.xzx;

    vec4 j = p - 49.0 * floor(p * ns.z * ns.z); //  mod(p,N*N)

    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_); // mod(j,N)

    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);

    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);

    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));

    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);

    // Normalise gradients
    vec4 norm =
    taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    // Mix final noise value
    vec4 m =
    max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 *
    dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

vec3 accel(float h2, vec3 pos) {
    float r2 = dot(pos, pos);
    float r5 = pow(r2, 2.5);
    return -1.5 * h2 * pos / r5 * 1.0;
}

vec3 toSpherical(vec3 p) {
    float rho = sqrt((p.x * p.x) + (p.y * p.y) + (p.z * p.z));
    if (rho < EPSILON) {
        return vec3(0.0, 0.0, 0.0);
    }
    float theta = atan(p.z, p.x);
    float phi = asin(clamp(p.y / rho, -1.0, 1.0));
    return vec3(rho, theta, phi);
}

void adiskColor(vec3 pos, inout vec3 color, inout float alpha) {
    
    float iscoRadius = 3.0 * eventHorizonRadius;
    float outerRadius = 12.0;
    float distanceFromCenter = length(pos);
    
    if (distanceFromCenter < iscoRadius) {
        return;
    }
    
    float iscoTransitionZone = iscoRadius * 1.2;
    float iscoFactor = smoothstep(iscoRadius, iscoTransitionZone, distanceFromCenter);
    float density = max(
        0.0, 1.0 - length(pos.xyz / vec3(outerRadius, adiskHeight, outerRadius)));
    if (density < 0.001) {
        return;
    }

    density *= pow(1.0 - abs(pos.y) / adiskHeight, adiskDensityV);

    // Apply ISCO factor to create smooth transition
    density *= iscoFactor;

    // Avoid the shader computation when density is very small
    if (density < 0.001) {
        return;
    }

    vec3 sphericalCoord = toSpherical(pos);

    // Scale the rho and phi so that the particles appear to be at the correct scale visually
    //sphericalCoord.y *= 2.0;
    //sphericalCoord.z *= 4.0;

    density *= 1.0 / pow(sphericalCoord.x, adiskDensityH);
    density *= 5000.0;

    if (adiskParticle < 0.5) {
        color += vec3(0.0, 1.0, 0.0) * density * 0.02;
        return;
    }

    float noise = 1.0;
    for (int i = 0; i < int(adiskNoiseLOD); i++) {
        noise *= 0.5 * snoise(sphericalCoord * pow(i, 2) * adiskNoiseScale) + 0.5;
        if (i % 2 == 0) {
            sphericalCoord.y += time * adiskSpeed;
        } else {
            sphericalCoord.y -= time * adiskSpeed;
        }
    }

    vec3 dustColor = texture(colorMap, vec2(sphericalCoord.x / outerRadius, 0.5)).rgb;

    color += density * adiskLit * dustColor * alpha * abs(noise);
}

vec3 traceColor(vec3 pos, vec3 dir) {
    vec3 color = vec3(0.0);
    float alpha = 1.0;
    bool hitEventHorizon = false;

    // Black hole center - use the uniform event horizon radius
    vec3 blackHoleCenter = vec3(0.0, 0.0, 0.0);

    // Fixed step size to prevent artifacts - scale with event horizon size
    float BASE_STEP_SIZE = min(0.1, eventHorizonRadius * 0.05);
    dir = normalize(dir);

    // Initial values for angular momentum conservation
    vec3 h = cross(pos - blackHoleCenter, dir);
    float h2 = dot(h, h);

    for (int i = 0; i < 500; i++) {
        if (renderBlackHole > 0.5) {
            float distToBlackHole = length(pos - blackHoleCenter);
            
            float stepScale = min(1.0, distToBlackHole / eventHorizonRadius * 0.5);
            float currentStepSize = BASE_STEP_SIZE * stepScale;

            if (gravatationalLensing > 0.5) {
                vec3 relativePos = pos - blackHoleCenter;
                vec3 acc = accel(h2, relativePos);
                dir += acc * currentStepSize;
                dir = normalize(dir); // Normalize but don't scale here
            }

            if (adiskEnabled > 0.5) {
                adiskColor(pos, color, alpha);
            }

            if (distToBlackHole < eventHorizonRadius) {
                hitEventHorizon = true;
                break;
            }
            
            pos += dir * currentStepSize;
        } else {
            pos += dir * BASE_STEP_SIZE;
        }

        if (length(pos - blackHoleCenter) > max(100.0, eventHorizonRadius * 50.0)) {
            break;
        }
    }
    
    if (!hitEventHorizon) {
        color += texture(galaxy, dir).rgb;
    } else if (length(color) < 0.01) {
        return vec3(0.0);
    }

    return color;
}

void main() {
    // Use camera uniforms for dynamic camera control
    vec2 uv = gl_FragCoord.xy / resolution.xy - vec2(0.5);
    uv.x *= cameraAspect;

    // Calculate ray direction using proper camera vectors and FOV
    float tanHalfFov = tan(radians(cameraFov) * 0.5);
    vec3 rayDir = normalize(cameraFront + cameraRight * uv.x * tanHalfFov + cameraUp * uv.y * tanHalfFov);
    vec3 rayPos = cameraPos;

    // Use the complex ray tracing function from blackhole_main.frag
    vec3 color = traceColor(rayPos, rayDir);
    fragColor = vec4(color, 1.0);
}