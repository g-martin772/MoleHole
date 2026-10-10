#version 460 core

in vec2 v_texCoord;
out vec4 FragColor;

uniform float u_time;
uniform float u_alpha;
uniform float u_lightIntensity;
uniform vec2 u_resolution;

// Noise function for nebula/space effect
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    
    for(int i = 0; i < 5; i++) {
        value += amplitude * noise(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }
    
    return value;
}

void main() {
    // Normalized pixel coordinates
    vec2 uv = (gl_FragCoord.xy / u_resolution.xy) * 2.0 - 1.0;
    uv.x *= u_resolution.x / u_resolution.y;
    
    // Planet position (right side of screen, inspired by Interstellar)
    vec2 planetCenter = vec2(0.9, 0.0);
    float planetRadius = 0.8;
    
    // Distance from planet center
    float dist = length(uv - planetCenter);
    
    // Create planet sphere
    float planet = smoothstep(planetRadius + 0.02, planetRadius - 0.02, dist);
    
    // Create atmospheric glow around planet edge
    float atmosphere = smoothstep(planetRadius + 0.15, planetRadius, dist);
    atmosphere -= planet;
    
    // Calculate lighting angle based on light intensity
    // Light comes from the left side gradually
    vec2 toPixel = normalize(uv - planetCenter);
    float lightAngle = dot(toPixel, vec2(-1.0, 0.3));
    
    // Create terminator (day/night boundary)
    float terminator = smoothstep(-0.1, 0.1, lightAngle);
    
    // Gradually illuminate the planet as light intensity increases
    float planetShading = mix(0.05, 1.0, terminator * u_lightIntensity);
    
    // Kepler-22b ocean world features
    // Create rotating UV for planet surface
    vec2 planetUV = (uv - planetCenter) / planetRadius;
    float planetDist = length(planetUV);
    
    if (planetDist < 1.0) {
        // Spherical mapping with rotation
        float phi = atan(planetUV.y, planetUV.x);
        float theta = acos(1.0 - planetDist * planetDist) / 1.57079632679;
        vec2 sphereUV = vec2(phi / 6.28318530718 + u_time * 0.02, theta);
        
        // Ocean base with multiple octaves for depth
        float oceanPattern = fbm(sphereUV * 6.0);
        float oceanDetail = fbm(sphereUV * 12.0 + oceanPattern * 0.5);
        
        // Cloud layers - Kepler-22b style thick atmosphere
        float cloudPattern = fbm(sphereUV * 4.0 + vec2(u_time * 0.015, 0.0));
        float cloudDetail = fbm(sphereUV * 8.0 + vec2(u_time * 0.025, u_time * 0.01));
        float clouds = cloudPattern * 0.7 + cloudDetail * 0.3;
        clouds = smoothstep(0.4, 0.7, clouds);
        
        // Kepler-22b ocean colors - vibrant blue-cyan
        vec3 deepOcean = vec3(0.05, 0.15, 0.35);      // Deep blue
        vec3 shallowOcean = vec3(0.15, 0.45, 0.65);   // Medium cyan-blue
        vec3 brightOcean = vec3(0.25, 0.65, 0.85);    // Bright cyan
        
        // Mix ocean colors based on patterns
        vec3 oceanColor = mix(deepOcean, shallowOcean, oceanPattern);
        oceanColor = mix(oceanColor, brightOcean, oceanDetail * 0.5);
        
        // Cloud colors - white with slight cyan tint
        vec3 cloudColor = vec3(0.9, 0.95, 1.0);
        vec3 cloudShadow = vec3(0.6, 0.7, 0.8);
        vec3 finalCloudColor = mix(cloudShadow, cloudColor, cloudDetail * 0.5 + 0.5);
        
        // Mix ocean and clouds
        vec3 surfaceColor = mix(oceanColor, finalCloudColor, clouds);
        
        // Add specular highlights on ocean (only where no clouds)
        float specular = pow(max(dot(toPixel, vec2(-1.0, 0.3)), 0.0), 32.0);
        specular *= (1.0 - clouds) * 0.6;
        surfaceColor += vec3(1.0, 1.0, 0.95) * specular * u_lightIntensity;
        
        // Apply lighting
        planetShading *= (0.85 + oceanDetail * 0.15);
        vec3 planetDarkColor = surfaceColor * 0.05;
        vec3 planetLitColor = surfaceColor * 1.2;
        vec3 planetColor = mix(planetDarkColor, planetLitColor, planetShading);
        
        // Add subtle rim lighting for 3D effect
        float rimFactor = 1.0 - planetDist;
        rimFactor = pow(rimFactor, 3.0);
        planetColor *= (1.0 + rimFactor * 0.3);
    } else {
        // Fallback for pixels outside planet
        vec3 planetColor = vec3(0.0);
    }
    
    // Atmospheric glow color - bright cyan-blue like Kepler-22b
    vec3 atmosphereColor = vec3(0.3, 0.7, 0.95) * u_lightIntensity;
    
    // Create nebula/space background with warmer tones
    vec2 nebulaUV = uv * 0.5 + vec2(u_time * 0.01, 0.0);
    float nebula = fbm(nebulaUV * 2.0);
    vec3 nebulaColor = vec3(0.08, 0.12, 0.18) * nebula;
    
    // Add stars
    float stars = 0.0;
    for(int i = 0; i < 3; i++) {
        vec2 starUV = uv * (10.0 + float(i) * 5.0);
        float starNoise = hash(floor(starUV));
        if(starNoise > 0.98) {
            vec2 starPos = fract(starUV);
            float starDist = length(starPos - 0.5);
            stars += smoothstep(0.05, 0.0, starDist) * (starNoise - 0.98) * 50.0;
        }
    }
    vec3 starColor = vec3(1.0) * stars;
    
    // Combine all elements
    vec3 color = nebulaColor + starColor;
    color = mix(color, atmosphereColor, atmosphere * 0.6);
    
    // Add planet with proper blending
    if (dist < planetRadius) {
        vec2 planetUV = (uv - planetCenter) / planetRadius;
        float planetDist = length(planetUV);
        
        if (planetDist < 1.0) {
            // Recalculate planet color (same as above)
            float phi = atan(planetUV.y, planetUV.x);
            float theta = acos(1.0 - planetDist * planetDist) / 1.57079632679;
            vec2 sphereUV = vec2(phi / 6.28318530718 + u_time * 0.02, theta);
            
            float oceanPattern = fbm(sphereUV * 6.0);
            float oceanDetail = fbm(sphereUV * 12.0 + oceanPattern * 0.5);
            float cloudPattern = fbm(sphereUV * 4.0 + vec2(u_time * 0.015, 0.0));
            float cloudDetail = fbm(sphereUV * 8.0 + vec2(u_time * 0.025, u_time * 0.01));
            float clouds = smoothstep(0.4, 0.7, cloudPattern * 0.7 + cloudDetail * 0.3);
            
            vec3 deepOcean = vec3(0.05, 0.15, 0.35);
            vec3 shallowOcean = vec3(0.15, 0.45, 0.65);
            vec3 brightOcean = vec3(0.25, 0.65, 0.85);
            vec3 oceanColor = mix(deepOcean, shallowOcean, oceanPattern);
            oceanColor = mix(oceanColor, brightOcean, oceanDetail * 0.5);
            
            vec3 cloudColor = vec3(0.9, 0.95, 1.0);
            vec3 cloudShadow = vec3(0.6, 0.7, 0.8);
            vec3 finalCloudColor = mix(cloudShadow, cloudColor, cloudDetail * 0.5 + 0.5);
            vec3 surfaceColor = mix(oceanColor, finalCloudColor, clouds);
            
            vec2 toPixelLocal = normalize(planetUV);
            float lightAngleLocal = dot(toPixelLocal, vec2(-1.0, 0.3));
            float terminatorLocal = smoothstep(-0.1, 0.1, lightAngleLocal);
            float planetShadingLocal = mix(0.05, 1.0, terminatorLocal * u_lightIntensity);
            planetShadingLocal *= (0.85 + oceanDetail * 0.15);
            
            float specular = pow(max(lightAngleLocal, 0.0), 32.0) * (1.0 - clouds) * 0.6;
            surfaceColor += vec3(1.0, 1.0, 0.95) * specular * u_lightIntensity;
            
            vec3 planetDarkColor = surfaceColor * 0.05;
            vec3 planetLitColor = surfaceColor * 1.2;
            vec3 planetColor = mix(planetDarkColor, planetLitColor, planetShadingLocal);
            
            float rimFactor = pow(1.0 - planetDist, 3.0);
            planetColor *= (1.0 + rimFactor * 0.3);
            
            color = planetColor;
        }
    }
    
    // Add stronger rim light for Kepler-22b's thick atmosphere
    float rimLight = atmosphere * u_lightIntensity * 1.2;
    color += vec3(0.4, 0.75, 1.0) * rimLight;
    
    // Apply overall fade
    color *= u_alpha;
    
    FragColor = vec4(color, u_alpha);
}

