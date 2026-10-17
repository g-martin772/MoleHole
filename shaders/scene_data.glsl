// std140-compatible:
// MoleHole::RaytraceParamsGpu (src/rendering/render_data.cppm)
struct BlackHoleData
{
    vec3 position;
    float mass;
    vec3 spinAxis;
    float spin;          // a / M of the active metric (clamped on the host)
    float charge;        // Q / M of the active metric (clamped on the host)
    float horizonRadius; // outer horizon r+
    float iscoRadius;    // prograde innermost stable circular orbit
    float massSolar;     // physical mass in solar masses
};

struct SphereData
{
    vec3 position;
    float radius;
    vec4 color;
    float mass;
    float _pad0;
    float _pad1;
    float _pad2;
};
