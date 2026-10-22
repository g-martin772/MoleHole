layout(set = 0, binding = 0, rgba32f) uniform image2D l_outputImage;

layout(set = 0, binding = 1, std140) uniform RaytraceParamsBlock
{
    vec3 u_cameraPos; float u_fov;
    vec3 u_cameraFront; float u_aspect;
    vec3 u_cameraUp; float u_flatPhase;
    vec3 u_cameraRight; float u_flatPhaseFast;

    int u_enableThirdPerson;
    float u_thirdPersonDistance;
    float u_thirdPersonHeight;
    float u_cubeSize;

    int u_isPhysicallyAccurate;
    int u_debugMode; // 0 off, 1 step count, 2 termination reason
    int u_metric_type;
    int u_gravitationalLensingEnabled;

    int u_accretionDiskEnabled;
    int u_accretionDiskVolumetric;
    int u_renderBlackHoles;
    int u_renderSpheres;

    int u_gravitationalRedshiftEnabled;
    float u_accDiskHeight;
    float u_accDiskNoiseScale;
    float u_accDiskNoiseLOD;

    float u_accDiskSpeed;
    float u_dopplerBeamingEnabled;
    float u_lutTempMin;
    float u_lutTempMax;

    float u_lutRedshiftMin;
    float u_lutRedshiftMax;
    float u_rayStepSize;
    int u_maxRaySteps;

    float u_adaptiveStepRate;
    int u_numBlackHoles;
    int u_numSpheres;
    float _pad0;

    BlackHoleData u_blackHoles[8];
    SphereData u_spheres[64];

    mat4 u_viewProjection;

    vec4 u_meshBounds;
    int u_numMeshInstances;
    float _meshPad0;
    float _meshPad1;
    float _meshPad2;

    float u_diskAccretionRate;     // Eddington ratio
    float u_diskOpacity;           // vertical optical depth
    float u_diskDisplayTemperature; // K of the hottest gas; 0 = physical
    int u_diskHotSpots;

    vec4 u_diskInfo[8];      // x = peak temperature [K], y = inner radius (ISCO), z = outer radius
    vec4 u_diskProfile[128]; // per hole 64 samples of T / peak, log-spaced between u_diskInfo.y and .z
    vec4 u_diskPhase[2048];  // per hole 256 bins of the gas rotation angle: x whole turns, y remainder, z advance to the next bin

    // Camera velocity relative to the local ZAMO of the first hole along (e_r, e_theta, e_phi), in c;
    // w = 1 when it is not the ZAMO itself (src/physics/observer.cppm)
    vec4 u_observerVelocity;

    // Characteristic radii of the first hole (FillOrbitOverlayData): photon orbit pro/retro, marginally bound
    // pro/retro; ISCO pro/retro, z = 1 when the overlay is on
    vec4 u_orbitRadii;
    vec4 u_orbitRadii2;

    // Lensed trails (shaders/trails.glsl): x = grid half-size (integration radius, render units), y = cells per axis,
    // z = 1 when there are traced trails, w = line half-width in radians
    vec4 u_trailGrid;
    vec4 u_trailGrid2; // x = hash table size (power of two)
};

layout(set = 0, binding = 2) uniform sampler2D u_skyboxTexture;
layout(set = 0, binding = 3) uniform sampler2D u_blackbodyLUT;
layout(set = 0, binding = 5) uniform sampler2D u_hrDiagramLUT;

layout(push_constant) uniform SlicePushBlock
{
    uint u_firstWorkgroup;
    uint u_workgroupsPerRow;
};

const float FAR_DEPTH = 0.999999f;

bool traceTexel(out ivec2 texCoords) {
    uint workgroup = u_firstWorkgroup + gl_WorkGroupID.x;
    uvec2 workgroupCoords = uvec2(workgroup % u_workgroupsPerRow, workgroup / u_workgroupsPerRow);
    texCoords = ivec2(workgroupCoords * 16u + gl_LocalInvocationID.xy);
    ivec2 imageSize = imageSize(l_outputImage);
    return texCoords.x < imageSize.x && texCoords.y < imageSize.y;
}

void cameraRay(ivec2 texCoords, out vec3 rayOrigin, out vec3 rayDir) {
    ivec2 imageSize = imageSize(l_outputImage);
    vec2 uv = (vec2(texCoords) + 0.5f) / vec2(imageSize);
    uv = uv * 2.0f - 1.0f;
    uv.x *= u_aspect;
    uv.y = -uv.y;

    vec3 cameraFront;
    vec3 cameraUp;
    vec3 cameraRight;

    if (u_enableThirdPerson == 1) {
        vec3 cubeRight = normalize(cross(u_cameraFront, u_cameraUp));
        vec3 actualCubeUp = normalize(cross(cubeRight, u_cameraFront));

        vec3 offsetBack = -u_cameraFront * u_thirdPersonDistance;
        vec3 offsetUp = actualCubeUp * u_thirdPersonHeight;
        rayOrigin = u_cameraPos + offsetBack + offsetUp;

        vec3 lookAtPoint = u_cameraPos + u_cameraFront * 1.0;
        cameraFront = normalize(lookAtPoint - rayOrigin);
        cameraUp = actualCubeUp;
        cameraRight = normalize(cross(cameraFront, cameraUp));
        cameraUp = normalize(cross(cameraRight, cameraFront));
    } else {
        rayOrigin = u_cameraPos;
        cameraFront = u_cameraFront;
        cameraUp = u_cameraUp;
        cameraRight = u_cameraRight;
    }

    float tanHalfFov = tan(radians(u_fov) * 0.5);
    rayDir = normalize(cameraFront + cameraRight * uv.x * tanHalfFov + cameraUp * uv.y * tanHalfFov);
}

float hitDepth(vec3 hitPoint) {
    vec4 clipPos = u_viewProjection * vec4(hitPoint, 1.0f);
    return clipPos.z / clipPos.w;
}

// A body's sky probe (GPP_BODY_PROBE) runs the same tracer from the body's centre: the body itself, the camera's
// observer, debug views, overlays and trails are left out, and the body's own velocity takes the observer's place.
#ifdef GPP_BODY_PROBE
const bool PROBE_PASS = true;
#else
const bool PROBE_PASS = false;
#endif
int g_skipSphere = -1;
vec4 g_probeVelocity = vec4(0.0);
bool g_bodyObserver = false; // the camera sits in a body's local frame and moves with it (docs/notes/local_frames.md)

vec4 traceObserverVelocity() { return (PROBE_PASS || g_bodyObserver) ? g_probeVelocity : u_observerVelocity; }
int traceDebugMode() { return PROBE_PASS ? 0 : u_debugMode; }
bool traceOrbitOverlay() { return !PROBE_PASS && u_orbitRadii2.z > 0.5f; }
bool traceThirdPerson() { return !PROBE_PASS && u_enableThirdPerson == 1; }
