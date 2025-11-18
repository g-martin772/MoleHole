#version 460 core
in vec3 Normal;
in float Mass;
out vec4 FragColor;

uniform vec3 uColor;
uniform sampler2D u_hrDiagramLUT; // HR diagram LUT: mass -> temperature, luminosity, radius
uniform sampler2D u_blackbodyLUT; // Blackbody LUT: temperature, redshift -> color
uniform int u_useHRDiagramLUT; // Toggle for HR diagram usage

// HR Diagram LUT parameters (must match HRDiagramLUTGenerator constants)
const float HR_MASS_MIN = 0.08;
const float HR_MASS_MAX = 100.0;
const float HR_LOG_MASS_MIN = log(HR_MASS_MIN);
const float HR_LOG_MASS_MAX = log(HR_MASS_MAX);

// Blackbody LUT parameters
uniform float u_lutTempMin = 1000.0;
uniform float u_lutTempMax = 40000.0;
uniform float u_lutRedshiftMin = 0.1;
uniform float u_lutRedshiftMax = 3.0;

// Get temperature from mass using HR diagram
float getTemperatureFromMass(float mass) {
    if (u_useHRDiagramLUT == 1 && mass > 0.0) {
        // Map mass to [0,1] using log scale (matching the generator)
        float logMass = log(clamp(mass, HR_MASS_MIN, HR_MASS_MAX));
        float massNorm = (logMass - HR_LOG_MASS_MIN) / (HR_LOG_MASS_MAX - HR_LOG_MASS_MIN);
        massNorm = clamp(massNorm, 0.0, 1.0);
        
        // Sample the LUT (returns vec3: temperature, luminosity, radius)
        vec3 hrData = texture(u_hrDiagramLUT, vec2(massNorm, 0.5)).rgb;
        return hrData.r; // temperature
    }
    return 0.0; // No valid temperature
}

vec3 getBlackbodyColorFromTemperature(float temperature) {
    if (temperature > 0.0) {
        // Map temperature to [0,1] range for LUT lookup
        float tempRange = u_lutTempMax - u_lutTempMin;
        float tempNorm = (temperature - u_lutTempMin) / tempRange;
        tempNorm = clamp(tempNorm, 0.0, 1.0);
        
        // Use redshift = 1.0 (no redshift) for static spheres
        float redshiftNorm = (1.0 - u_lutRedshiftMin) / (u_lutRedshiftMax - u_lutRedshiftMin);
        redshiftNorm = clamp(redshiftNorm, 0.0, 1.0);
        
        // Sample the blackbody LUT
        return texture(u_blackbodyLUT, vec2(tempNorm, redshiftNorm)).rgb;
    }
    return vec3(1.0); // White fallback
}

void main() {
    
    vec3 finalColor = uColor;
    
    // If HR diagram LUT is enabled and mass is valid, use it to determine color
    if (u_useHRDiagramLUT == 1 && Mass > 0.0) {
        float temp = getTemperatureFromMass(Mass);
        if (temp > 0.0) {
            vec3 hrColor = getBlackbodyColorFromTemperature(temp);
            // Blend HR color with base color, or use HR color directly
            finalColor = hrColor;
        }
    }
    
    FragColor = vec4(finalColor, 1.0);
}
