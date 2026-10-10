// std140-compatible:
// MoleHole::RaytraceParamsGpu (src/rendering/render_data.cppm)
struct BlackHoleData
{
    vec3 position;
    float mass;
    vec3 spinAxis;
    float spin;
    float charge;
    float _pad0;
    float _pad1;
    float _pad2;
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
