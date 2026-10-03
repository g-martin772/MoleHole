// ------------------------------------------------------------------------------------------------------------
// Section Colour
// ------------------------------------------------------------------------------------------------------------
uniform float u_lutTempMin = 1000.0;
uniform float u_lutTempMax = 40000.0;
uniform float u_lutRedshiftMin = 0.1;
uniform float u_lutRedshiftMax = 3.0;

uniform sampler2D u_blackbodyLUT;

vec3 getBlackbodyColorLUT(float temperature, float redshiftFactor) {
    float tempRange = u_lutTempMax - u_lutTempMin;
    float tempNorm = (temperature - u_lutTempMin) / tempRange;
    tempNorm = max(0.0f, min(1.0f, tempNorm));

    float redshiftRange = u_lutRedshiftMax - u_lutRedshiftMin;
    float redshiftNorm = (redshiftFactor - u_lutRedshiftMin) / redshiftRange;
    redshiftNorm = max(0.0f, min(1.0f, redshiftNorm));

    vec3 color = texture(u_blackbodyLUT, vec2(tempNorm, redshiftNorm)).rgb;
    return color;
}

// ------------------------------------------------------------------------------------------------------------
// Section Acceleration
// ------------------------------------------------------------------------------------------------------------
const float ACC_LUT_R_MIN = 0.01;
const float ACC_LUT_R_MAX = 50.0;
const float ACC_LUT_ANG_MOM_MIN = 0.0;
const float ACC_LUT_ANG_MOM_MAX = 100.0;
const float ACC_LUT_LOG_R_MIN = log(ACC_LUT_R_MIN);
const float ACC_LUT_LOG_R_MAX = log(ACC_LUT_R_MAX);

uniform sampler2D u_accelerationLUT;

vec3 calculateAccelerationLUT(float angMomentumSqrd, vec3 relPos) {
    float rSqrd = dot(relPos, relPos);
    float r = sqrt(rSqrd);

    float angMomNorm = (angMomentumSqrd - ACC_LUT_ANG_MOM_MIN) / (ACC_LUT_ANG_MOM_MAX - ACC_LUT_ANG_MOM_MIN);
    angMomNorm = clamp(angMomNorm, 0.0, 1.0);

    float logR = log(max(r, ACC_LUT_R_MIN));
    float rNorm = (logR - ACC_LUT_LOG_R_MIN) / (ACC_LUT_LOG_R_MAX - ACC_LUT_LOG_R_MIN);
    rNorm = clamp(rNorm, 0.0, 1.0);

    float factor = texture(u_accelerationLUT, vec2(angMomNorm, rNorm)).r;
    return factor * relPos;
}

// ------------------------------------------------------------------------------------------------------------
// Section HR Diagram
// ------------------------------------------------------------------------------------------------------------
const float HR_MASS_MIN = 0.08;
const float HR_MASS_MAX = 100.0;
const float HR_LOG_MASS_MIN = log(HR_MASS_MIN);
const float HR_LOG_MASS_MAX = log(HR_MASS_MAX);

uniform sampler2D u_hrDiagramLUT;

float getTemperatureFromMass(float mass) {
    float logMass = log(clamp(mass, HR_MASS_MIN, HR_MASS_MAX));
    float massNorm = (logMass - HR_LOG_MASS_MIN) / (HR_LOG_MASS_MAX - HR_LOG_MASS_MIN);
    massNorm = clamp(massNorm, 0.0, 1.0);

    vec3 hrData = texture(u_hrDiagramLUT, vec2(massNorm, 0.5)).rgb;
    return hrData.r;
}