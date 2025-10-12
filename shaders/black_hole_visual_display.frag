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
uniform float eventHorizonRadius = 4.0;  // Controllable event horizon radius

uniform float adiskEnabled = 1.0;
uniform float adiskParticle = 1.0;
uniform float adiskHeight = 0.2;
uniform float adiskLit = 0.5;
uniform float adiskDensityV = 1.0;
uniform float adiskDensityH = 1.0;
uniform float adiskNoiseScale = 1.0;
uniform float adiskNoiseLOD = 5.0;
uniform float adiskSpeed = 0.5;

struct Ring {
    vec3 center;
    vec3 normal;
    float innerRadius;
    float outerRadius;
    float rotateSpeed;
};

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

// Volumetric cloud noise functions
float fbm(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * snoise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}

float worleyNoise(vec3 p) {
    vec3 id = floor(p);
    vec3 f = fract(p);

    float minDist = 1.0;
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            for (int z = -1; z <= 1; z++) {
                vec3 neighbor = vec3(float(x), float(y), float(z));
                vec3 point = vec3(snoise(id + neighbor));
                point = 0.5 + 0.5 * sin(time * 0.5 + 6.2831 * point);
                vec3 diff = neighbor + point - f;
                float dist = length(diff);
                minDist = min(minDist, dist);
            }
        }
    }
    return minDist;
}

float cloudDensity(vec3 pos, float time) {
    // Optimized: Reduced octaves from 6 to 4 for base clouds for better performance
    float baseClouds = fbm(pos * 0.25 + vec3(time * 0.06, 0.0, time * 0.03), 4); // Reduced from 6 to 4 octaves

    // Optimized: Reduced detail layers and octaves for performance
    float detailNoise1 = fbm(pos * 1.0 + vec3(time * 0.12, 0.0, time * 0.06), 3); // Reduced from 4 to 3 octaves
    float detailNoise2 = fbm(pos * 2.5 + vec3(time * 0.18, 0.0, time * 0.09), 2); // Reduced from 3 to 2 octaves

    // Worley noise for enhanced cellular structure (kept for quality)
    float worley = worleyNoise(pos * 0.6 + vec3(time * 0.02, 0.0, time * 0.04));

    // More sophisticated noise blending for higher detail with increased density
    float density = baseClouds * 0.8 + detailNoise1 * 0.35 + detailNoise2 * 0.2 + (1.0 - worley) * 0.25;

    // Softer transitions with multiple thresholds for better detail preservation and higher density
    density = smoothstep(0.02, 0.4, density) * smoothstep(1.0, 0.6, density);

    return clamp(density, 0.0, 1.0);
}

vec3 cloudColor(vec3 pos, vec3 lightDir, float density) {
    // Base cloud color
    vec3 baseColor = vec3(0.9, 0.95, 1.0);

    // Add some variation based on density
    vec3 darkColor = vec3(0.3, 0.4, 0.6);
    vec3 lightColor = vec3(1.0, 0.98, 0.95);

    // Simple lighting approximation
    float lightPenetration = exp(-density * 4.0);
    vec3 finalColor = mix(darkColor, lightColor, lightPenetration);

    // Add some atmospheric scattering effect
    float scattering = pow(max(0.0, dot(normalize(pos), lightDir)), 2.0);
    finalColor += vec3(0.2, 0.3, 0.5) * scattering * density;

    return finalColor;
}
///----

float ringDistance(vec3 rayOrigin, vec3 rayDir, Ring ring) {
    float denominator = dot(rayDir, ring.normal);
    float constant = -dot(ring.center, ring.normal);
    if (abs(denominator) < EPSILON) {
        return -1.0;
    } else {
        float t = -(dot(rayOrigin, ring.normal) + constant) / denominator;
        if (t < 0.0) {
            return -1.0;
        }

        vec3 intersection = rayOrigin + t * rayDir;

        // Compute distance to ring center
        float d = length(intersection - ring.center);
        if (d >= ring.innerRadius && d <= ring.outerRadius) {
            return t;
        }
        return -1.0;
    }
}

vec3 panoramaColor(sampler2D tex, vec3 dir) {
    vec2 uv = vec2(0.5 - atan(dir.z, dir.x) / PI * 0.5, 0.5 - asin(dir.y) / PI);
    return texture2D(tex, uv).rgb;
}

vec3 accel(float h2, vec3 pos) {
    float r2 = dot(pos, pos);
    float r = sqrt(r2);

    // Prevent division by zero and numerical instability
    if (r < eventHorizonRadius * 0.1) {
        return vec3(0.0);
    }

    float r3 = r2 * r;
    float r5 = r2 * r3;

    // Improved gravitational acceleration with proper photon sphere physics
    // The photon sphere is at r = 1.5 * eventHorizonRadius for Schwarzschild metric
    float photonSphereRadius = 1.5 * eventHorizonRadius;

    // Base Newtonian-like term with relativistic corrections
    vec3 acc = -1.5 * h2 * pos / r5;

    // Add proper relativistic correction term that creates the photon sphere
    // This term becomes critical near r = 1.5 * Rs
    float relativisticFactor = 1.0 + 1.5 * (eventHorizonRadius * eventHorizonRadius) / r2;
    acc += -pos / r3 * relativisticFactor;

    // Additional photon sphere enhancement for visual accuracy
    // Creates the characteristic ring of enhanced lensing
    float photonSphereFactor = exp(-pow((r - photonSphereRadius) / (eventHorizonRadius * 0.1), 2.0));
    acc *= (1.0 + photonSphereFactor * 0.5);

    return acc;
}

vec4 quadFromAxisAngle(vec3 axis, float angle) {
    vec4 qr;
    float half_angle = (angle * 0.5) * 3.14159 / 180.0;
    qr.x = axis.x * sin(half_angle);
    qr.y = axis.y * sin(half_angle);
    qr.z = axis.z * sin(half_angle);
    qr.w = cos(half_angle);
    return qr;
}

vec4 quadConj(vec4 q) { return vec4(-q.x, -q.y, -q.z, q.w); }

vec4 quat_mult(vec4 q1, vec4 q2) {
    vec4 qr;
    qr.x = (q1.w * q2.x) + (q1.x * q2.w) + (q1.y * q2.z) - (q1.z * q2.y);
    qr.y = (q1.w * q2.y) - (q1.x * q2.z) + (q1.y * q2.w) + (q1.z * q2.x);
    qr.z = (q1.w * q2.z) + (q1.x * q2.y) - (q1.y * q2.x) + (q1.z * q2.w);
    qr.w = (q1.w * q2.w) - (q1.x * q2.x) - (q1.y * q2.y) - (q1.z * q2.z);
    return qr;
}

vec3 rotateVector(vec3 position, vec3 axis, float angle) {
    vec4 qr = quadFromAxisAngle(axis, angle);
    vec4 qr_conj = quadConj(qr);
    vec4 q_pos = vec4(position.x, position.y, position.z, 0);

    vec4 q_tmp = quat_mult(qr, q_pos);
    qr = quat_mult(q_tmp, qr_conj);

    return vec3(qr.x, qr.y, qr.z);
}

#define IN_RANGE(x, a, b) (((x) > (a)) && ((x) < (b)))

void cartesianToSpherical(in vec3 xyz, out float rho, out float phi,
out float theta) {
    rho = sqrt((xyz.x * xyz.x) + (xyz.y * xyz.y) + (xyz.z * xyz.z));
    phi = asin(xyz.y / rho);
    theta = atan(xyz.z, xyz.x);
}

// Convert from Cartesian to spherical coord (rho, phi, theta)
// https://en.wikipedia.org/wiki/Spherical_coordinate_system
vec3 toSpherical(vec3 p) {
    float rho = sqrt((p.x * p.x) + (p.y * p.y) + (p.z * p.z));
    
    // Prevent division by zero for points at origin
    if (rho < EPSILON) {
        return vec3(0.0, 0.0, 0.0);
    }
    
    float theta = atan(p.z, p.x);
    
    // Clamp y/rho to prevent asin domain errors
    float phi = asin(clamp(p.y / rho, -1.0, 1.0));
    
    return vec3(rho, theta, phi);
}

vec3 toSpherical2(vec3 pos) {
    vec3 radialCoords;
    radialCoords.x = length(pos) * 1.5 + 0.55;
    radialCoords.y = atan(-pos.x, -pos.z) * 1.5;
    radialCoords.z = abs(pos.y);
    return radialCoords;
}

void ringColor(vec3 rayOrigin, vec3 rayDir, Ring ring, inout float minDistance,
inout vec3 color) {
    float distance = ringDistance(rayOrigin, normalize(rayDir), ring);
    if (distance >= EPSILON && distance < minDistance &&
    distance <= length(rayDir) + EPSILON) {
        minDistance = distance;

        vec3 intersection = rayOrigin + normalize(rayDir) * minDistance;
        vec3 ringColor;

        {
            float dist = length(intersection);

            float v = clamp((dist - ring.innerRadius) /
            (ring.outerRadius - ring.innerRadius),
            0.0, 1.0);

            vec3 base = cross(ring.normal, vec3(0.0, 0.0, 1.0));
            float angle = acos(dot(normalize(base), normalize(intersection)));
            if (dot(cross(base, intersection), ring.normal) < 0.0)
            angle = -angle;

            float u = 0.5 - 0.5 * angle / PI;
            // HACK
            u += time * ring.rotateSpeed;

            vec3 color = vec3(0.0, 0.5, 0.0);
            // HACK
            float alpha = 0.5;
            ringColor = vec3(color);
        }

        color += ringColor;
    }
}

mat3 lookAt(vec3 origin, vec3 target, float roll) {
    vec3 rr = vec3(sin(roll), cos(roll), 0.0);
    vec3 ww = normalize(target - origin);
    vec3 uu = normalize(cross(ww, rr));
    vec3 vv = normalize(cross(uu, ww));

    return mat3(uu, vv, ww);
}

float sqrLength(vec3 a) { return dot(a, a); }

void adiskColor(vec3 pos, inout vec3 color, inout float alpha) {
    // Calculate Innermost Stable Circular Orbit (ISCO)
    // For a Schwarzschild black hole: R_isco = 6 * R_schwarzschild = 3 * R_event_horizon
    float iscoRadius = 3.0 * eventHorizonRadius;

    float outerRadius = 12.0 * eventHorizonRadius;

    // Get distance from black hole center
    float distanceFromCenter = length(pos);
    
    // Completely exclude any rendering inside ISCO with a smooth transition
    if (distanceFromCenter < iscoRadius) {
        return; // No accretion disk material inside ISCO
    }
    
    // Add a smooth transition zone just outside ISCO to avoid hard edges
    float iscoTransitionZone = iscoRadius * 1.2; // 20% transition zone
    float iscoFactor = smoothstep(iscoRadius, iscoTransitionZone, distanceFromCenter);
    
    // Check if we're within the disk height using volumetric approach with increased thickness
    float diskThickness = adiskHeight * 2.5; // Make disk 2.5x thicker vertically
    if (abs(pos.y) > diskThickness) {
        return;
    }

    // Calculate orbital velocity for Doppler beaming FIRST (using original position)
    // This keeps the beaming pattern fixed relative to the camera
    vec3 toCenter = normalize(-pos); // Use original position, not rotated
    vec3 orbitalVelocity = normalize(cross(vec3(0.0, 1.0, 0.0), toCenter)); // Perpendicular to radial direction

    // Keplerian velocity decreases with distance: v ∝ 1/√r
    float velocityMagnitude = sqrt(eventHorizonRadius / distanceFromCenter) * 0.3; // Scale factor for realistic velocities
    vec3 velocity = orbitalVelocity * velocityMagnitude;

    // Calculate Doppler factor: approaching material appears brighter, receding dimmer
    vec3 viewDirection = normalize(cameraPos - pos); // Use original position
    float velocityDotView = dot(velocity, viewDirection);

    // Relativistic Doppler beaming factor
    float beta = velocityMagnitude; // v/c (assuming c=1 in our units)
    float gamma = 1.0 / sqrt(1.0 - beta * beta);
    float dopplerFactor = gamma * (1.0 - beta * velocityDotView);

    // Apply Doppler beaming to intensity (δ^3 for synchrotron radiation)
    float beamingIntensity = pow(dopplerFactor, 3.0);

    // NOW do the noise rotation for structural animation (separate from physics)
    // Use volumetric cloud density for the accretion disk
    vec3 diskPos = pos;
    // Scale position for more coherent cloud structure with denser packing
    diskPos *= vec3(1.2, 4.0, 1.2); // Reduced Y scaling for thicker disk, slightly reduced X/Z for denser clouds

    // Add rotation to the disk based on distance from center (for noise animation only)
    float rotationSpeed = 1.0 / sqrt(distanceFromCenter) * adiskSpeed;
    float angle = time * rotationSpeed;
    float cosA = cos(angle);
    float sinA = sin(angle);
    diskPos.xz = mat2(cosA, -sinA, sinA, cosA) * diskPos.xz;

    // Optimized: Reduced noise complexity for better performance
    float diskDensity = cloudDensity(diskPos * 0.8, time) * 20; // Reduced from 25 to 20, increased sampling scale

    // Apply additional disk-specific density modifications with smoother transitions
    float radialFalloff = smoothstep(outerRadius, iscoRadius, distanceFromCenter); // Inverted smoothstep for better falloff
    float heightFalloff = smoothstep(diskThickness, 0.0, abs(pos.y)); // Use thicker disk height for falloff

    // Combine all density factors with optimized multiplier
    diskDensity *= radialFalloff * heightFalloff * iscoFactor * adiskDensityH * 3.0; // Reduced from 3.5 to 3.0

    if (diskDensity < 0.002) { // Increased threshold from 0.001 to 0.002 for early culling
        return;
    }

    // Enhanced disk coloring using cloud-based approach
    vec3 lightDir = normalize(vec3(1.0, 0.2, 0.3)); // Light source direction

    if (adiskParticle > 0.5) {
        // Create softer temperature-based orange coloring
        float temperature = 1.2 / pow(distanceFromCenter / eventHorizonRadius, 0.6); // Softer temperature curve

        // Create softer orange color gradient with more gradual transitions
        vec3 hotColor = vec3(0.6, 0.35, 0.1);
        vec3 warmColor = vec3(0.5, 0.25, 0.05);
        vec3 coolColor = vec3(0.3, 0.12, 0.03);

        vec3 temperatureColor;
        // Smoother temperature blending with wider transition zones
        if (temperature > 1.2) {
            float blend = smoothstep(1.2, 2.0, temperature);
            temperatureColor = mix(warmColor, hotColor, blend);
        } else {
            float blend = smoothstep(0.3, 1.2, temperature);
            temperatureColor = mix(coolColor, warmColor, blend);
        }

        // Get cloud structure for volume details with softer lighting
        vec3 diskCloudColor = cloudColor(diskPos, lightDir, diskDensity);

        // More balanced blending - less harsh contrast
        vec3 finalDiskColor = mix(temperatureColor, diskCloudColor * temperatureColor, 0.5);

        // Optimized: Reduced detail computation for performance
        float additionalDetail = fbm(diskPos * 3.0 + vec3(time * 0.3), 1) * 0.4 + 0.8; // Reduced from 2 octaves to 1, simpler scale
        finalDiskColor *= additionalDetail;

        // Softer saturation enhancement
        finalDiskColor *= vec3(1.0, 0.95, 0.85); // Less aggressive color boost

        // Apply Doppler beaming to the final color intensity with clamping
        float clampedBeaming = clamp(beamingIntensity, 0.3, 2.5); // Prevent extreme values
        finalDiskColor *= clampedBeaming;

        // Apply lighting with balanced intensity
        color += finalDiskColor * diskDensity * adiskLit * alpha * 0.22; // Reduced from 0.25 to 0.22
    } else {
        // Simple colored disk with Doppler beaming and optimized density
        vec3 simpleColor = vec3(0.4, 0.2, 0.0) * diskDensity * 0.018; // Reduced from 0.02 to 0.018
        float clampedBeaming = clamp(beamingIntensity, 0.3, 2.5);
        color += simpleColor * clampedBeaming;
    }
}

vec3 traceColor(vec3 pos, vec3 dir) {
    vec3 color = vec3(0.0);
    float alpha = 1.0;
    bool hitEventHorizon = false;

    // Black hole center - use the uniform event horizon radius
    vec3 blackHoleCenter = vec3(0.0, 0.0, 0.0);

    // Optimized: Increased step size for better performance while maintaining quality
    float BASE_STEP_SIZE = eventHorizonRadius * 0.04; // Increased from 0.02 to 0.04 for 2x performance boost
    dir = normalize(dir);

    // Initial values for angular momentum conservation
    vec3 h = cross(pos - blackHoleCenter, dir);
    float h2 = dot(h, h);

    // Optimized: Reduced maximum iterations for better performance
    for (int i = 0; i < 2000; i++) { // Reduced from 3000 to 2000 for 33% performance boost
        if (renderBlackHole > 0.5) {
            float distToBlackHole = length(pos - blackHoleCenter);
            
            // Optimized: Simplified adaptive step size calculation
            float stepScale = 1.0;
            if (distToBlackHole < eventHorizonRadius * 5.0) {
                // Fine steps near the black hole for disk detail
                stepScale = 0.5 + 0.5 * (distToBlackHole / (eventHorizonRadius * 5.0));
            } else if (distToBlackHole < eventHorizonRadius * 12.0) {
                // Medium resolution for disk regions - reduced from 15.0 to 12.0
                stepScale = 0.8 + 0.2 * ((distToBlackHole - eventHorizonRadius * 5.0) / (eventHorizonRadius * 7.0));
            }

            float currentStepSize = BASE_STEP_SIZE * stepScale;

            // If gravitational lensing is applied
            if (gravatationalLensing > 0.5) {
                vec3 relativePos = pos - blackHoleCenter;
                vec3 acc = accel(h2, relativePos);
                dir += acc * currentStepSize;
                dir = normalize(dir);
            }

            // Render accretion disk BEFORE checking event horizon
            if (adiskEnabled > 0.5) {
                adiskColor(pos, color, alpha);
            }

            // Check if we've hit the event horizon AFTER rendering disk
            if (distToBlackHole < eventHorizonRadius) {
                hitEventHorizon = true;
                break;
            }
            
            // Apply step with consistent direction
            pos += dir * currentStepSize;
        } else {
            pos += dir * BASE_STEP_SIZE;
        }

        // Optimized: Earlier break for distant rays - reduced from 100.0 to 50.0
        if (length(pos - blackHoleCenter) > max(40.0, eventHorizonRadius * 50.0)) {
            break;
        }
    }

    // Only sample skybox color if we didn't hit the event horizon
    if (!hitEventHorizon) {
        color += texture(galaxy, dir).rgb;
    } else if (length(color) < 0.01) {
        return vec3(0.0);
    }

    return color;
}

void main() {
    // Use camera uniforms for dynamic camera control
    vec2 uv = (gl_FragCoord.xy - resolution.xy * 0.5) / min(resolution.x, resolution.y);

    // Calculate ray direction using proper camera vectors and FOV
    float tanHalfFov = tan(radians(cameraFov) * 0.5);
    vec3 rayDir = normalize(cameraFront + cameraRight * uv.x * tanHalfFov + cameraUp * uv.y * tanHalfFov);
    vec3 rayPos = cameraPos;

    // Use the complex ray tracing function from blackhole_main.frag
    vec3 color = traceColor(rayPos, rayDir);
    fragColor = vec4(color, 1.0);
}
