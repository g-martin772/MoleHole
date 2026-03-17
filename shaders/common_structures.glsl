#ifndef COMMON_STRUCTURES_GLSL
#define COMMON_STRUCTURES_GLSL

layout(binding = 1) uniform CommonUBO {
    mat4 u_view;
    mat4 u_projection;
    mat4 u_invView;
    mat4 u_invProjection;
    vec4 u_cameraPos; // w = time
    vec4 u_cameraFront;
    vec4 u_cameraUp;
    vec4 u_cameraRight;
    vec4 u_params; // x = fov, y = aspect, z = exposure, w = gamma
    vec4 u_resolution; // x = width, y = height
    
    // Black Hole Data (Max 8)
    int u_numBlackHoles;
    float u_blackHoleMasses[8];
    vec4 u_blackHolePositions[8]; // w = unused
    float u_blackHoleSpins[8];
    vec4 u_blackHoleSpinAxes[8]; // w = unused

    // Settings
    int u_renderBlackHoles;
    int u_renderSpheres;
    int u_accretionDiskEnabled;
    int u_gravitationalLensingEnabled;
    float u_cubeSize;
    
    // Ray Marching Settings
    float u_rayStepSize;
    int u_maxRaySteps;
    float u_adaptiveStepRate;
    
    // Third Person
    int u_enableThirdPerson;
    float u_thirdPersonDistance;
    float u_thirdPersonHeight;
    float padding; // Alignment
    
    // LUT Params
    float u_lutTempMin;
    float u_lutTempMax;
    float u_lutRedshiftMin;
    float u_lutRedshiftMax;
    
    // Accretion Disk Params
    int u_accretionDiskVolumetric;
    float u_accDiskHeight;
    float u_accDiskNoiseScale;
    float u_accDiskNoiseLOD;
    float u_accDiskSpeed;
    int u_gravitationalRedshiftEnabled;
    float u_dopplerBeamingEnabled;
    float padding2; // Alignment
    
    // Spheres Data (Max 16)
    int u_numSpheres;
    float padding3[3]; // Alignment
    vec4 u_spherePositions[16]; // w = radius
    vec4 u_sphereColors[16]; // w = mass

    // Debug
    int u_debugMode;
    float padding4[3];
};

#endif
