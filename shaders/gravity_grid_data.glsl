struct GravityGridPoint
{
    vec3 position;
    float mass;
};

layout(set = 0, binding = 0, std140) uniform GravityGridParamsBlock
{
    mat4 u_VP;

    float u_planeY;
    float u_cellSize;
    float u_lineThickness;
    float u_opacity;

    vec3 u_color;
    int u_numBlackHoles;

    int u_numSpheres;
    int u_numMeshes;
    float _pad0;
    float _pad1;

    GravityGridPoint u_blackHoles[8];
    GravityGridPoint u_spheres[16];
    GravityGridPoint u_meshes[8];
};
